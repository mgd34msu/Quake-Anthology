#include "internal.h"

bool q3_cancel_kamikaze_timers(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    for (uint32_t i = 0; i < game->capacity; ++i) {
        q3_actor *timer = &game->actors[i];
        if (timer->kind == Q3_ACTOR_KAMIKAZE_TIMER &&
            qa_actor_id_equal(timer->state.kamikaze.attacker, actor)) {
            qa_actor_id id = timer->actor;
            if (!qa_session_release(game->options.services.session, id, error))
                return false;
        }
    }
    return true;
}
static bool cancel_death_effects(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    if (!game)
        return q3_fail(error, "missing Q3 death effect provider");
    q3_actor *entry = q3_actor_get(game, actor);
    if (entry && entry->kind == Q3_ACTOR_PLAYER)
        entry->state.player.flags &= ~0x200u;
    return q3_cancel_kamikaze_timers(game, actor, error);
}
bool qa_q3_cancel_death_effects(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    if (!game || game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "invalid Q3 death-effect cancellation boundary");
    ++game->observation_depth;
    bool result = cancel_death_effects(game, actor, error);
    --game->observation_depth;
    return result;
}
bool q3_schedule_kamikaze(qa_q3_game *game, qa_actor_id actor, qa_vec3 origin, qa_error *error) {
    qa_builtin_spawn spawn = {.owner = game->options.owner, .body = {.origin = origin}};
    qa_actor_id timer;
    if (!qa_builtin_spawn_actor(&game->options.services, &spawn, &timer, error))
        return false;
    game->actors[timer.slot] =
        (q3_actor){.actor = timer,
                   .kind = Q3_ACTOR_KAMIKAZE_TIMER,
                   .alpha = 1,
                   .state.kamikaze = {.attacker = actor, .next = q3_add_time(game->now_ms, 5000)}};
    return true;
}
bool q3_death_rewards(qa_q3_game *game, qa_actor_id victim, const qa_damage_request *request,
                      qa_error *error) {
    q3_actor *entry = q3_actor_get(game, victim);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(game->options.services.world, victim, &body, error))
        return false;
    qa_actor_id look = request->attack.attacker;
    if (!look.registry || qa_actor_id_equal(look, victim))
        look = request->attack.inflictor;
    qa_body_state target;
    qa_error ignored = {0};
    float yaw = body.angles.y;
    if (look.registry && !qa_actor_id_equal(look, victim) &&
        qa_world_body_read(game->options.services.world, look, &target, &ignored)) {
        qa_vec3 delta = qa_vec_sub(target.origin, body.origin);
        yaw = atan2f(delta.y, delta.x) * 180 / Q3_PI;
        if (yaw < 0)
            yaw += 360;
    }
    entry->state.player.dead_yaw = (int32_t)fmodf(yaw, 360);
    entry->state.player.view_angles = qa_v3(0, body.angles.y, 0);
    int32_t method = request->attack.cause.kind == QA_CAUSE_Q3
                         ? request->attack.cause.source.q3.means_of_death
                         : 0;
    if (!q3_event(game, victim, request->attack.attacker, QA_BUILTIN_DEATH, 60, method, body.origin,
                  qa_v3(0, 0, 0), qa_v3(0, 0, 0), error))
        return false;
    entry = q3_actor_get(game, victim);
    q3_actor *killer = q3_actor_get(game, request->attack.attacker);
    if (!entry || !killer || killer->kind != Q3_ACTOR_PLAYER ||
        qa_actor_id_equal(victim, killer->actor))
        return true;
    qa_actor_id attacker = request->attack.attacker;
    qa_combat_state vc, kc;
    if (!qa_combat_read(game->options.services.combat, victim, &vc, error))
        return false;
    killer = q3_actor_get(game, attacker);
    if (!killer || killer->kind != Q3_ACTOR_PLAYER)
        return true;
    if (!qa_combat_read(game->options.services.combat, attacker, &kc, error))
        return false;
    entry = q3_actor_get(game, victim);
    killer = q3_actor_get(game, attacker);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER || !killer ||
        killer->kind != Q3_ACTOR_PLAYER)
        return true;
    if (game->options.rules.game_type >= 3 && vc.team == kc.team)
        return true;
    qa_q3_player_state *player = &killer->state.player;
    if (method == 2) {
        player->gauntlet_frag_count = q3_add_time(player->gauntlet_frag_count, 1);
        player->flags = (player->flags & ~0x38848u) | 0x40u;
        player->reward_until = q3_add_time(game->now_ms, 2000);
        entry->state.player.player_events ^= 2;
    }
    if (q3_sub_time(game->now_ms, player->last_kill_ms) < 3000) {
        player->excellent_count = q3_add_time(player->excellent_count, 1);
        player->flags = (player->flags & ~0x38848u) | 8u;
        player->reward_until = q3_add_time(game->now_ms, 2000);
        if (!q3_ranking_reward(game, attacker, 8u, error))
            return false;
    }
    player->last_kill_ms = game->now_ms;
    return true;
}

static bool drop_item(qa_q3_game *game, qa_actor_id player, uint32_t index, float yaw,
                      int32_t count, qa_error *error) {
    qa_body_state body;
    if (!qa_world_body_read(game->options.services.world, player, &body, error))
        return false;
    qa_vec3 forward;
    q3_source_angle_vectors(qa_v3(0, body.angles.y + yaw, body.angles.z), &forward, NULL, NULL);
    qa_vec3 velocity = qa_vec_scale(forward, 150);
    velocity.z += 200 + q3_crandom(game) * 50;
    qa_q3_item_spawn spawn = {.item_index = index,
                              .dropped = true,
                              .origin = body.origin,
                              .velocity = velocity,
                              .count = count};
    qa_actor_id item;
    return qa_q3_spawn_item(game, &spawn, &item, error);
}
bool q3_drop_player_items(qa_q3_game *game, qa_actor_id actor, bool no_drop, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    qa_q3_player_state player = entry->state.player;
    size_t count;
    const qa_q3_item *items = qa_q3_items(game->options.product, &count);
    if (!no_drop) {
        qa_q3_weapon weapon = player.weapon;
        if ((weapon == QA_Q3_W_MACHINEGUN || weapon == QA_Q3_W_GRAPPLE) &&
            player.weapon_phase == QA_Q3_DROPPING)
            weapon = player.requested_weapon;
        if ((player.selections & QA_Q3_ARSENAL) && weapon > QA_Q3_W_MACHINEGUN &&
            weapon != QA_Q3_W_GRAPPLE && q3_owns_weapon(game, actor, weapon)) {
            int32_t ammo;
            if (!q3_ammo_read(game, actor, weapon, &ammo, error))
                return false;
            if (ammo)
                for (size_t i = 1; i < count; ++i)
                    if (items[i].kind == QA_Q3_ITEM_WEAPON && items[i].tag == (int32_t)weapon) {
                        if (!drop_item(game, actor, (uint32_t)i, 0, 0, error))
                            return false;
                        break;
                    }
        }
        if (game->options.rules.game_type != 3) {
            float yaw = 45;
            for (unsigned powerup = 1; powerup <= QA_Q3_P_FLIGHT; ++powerup) {
                if (player.powerups[powerup] <= game->now_ms)
                    continue;
                for (size_t i = 1; i < count; ++i)
                    if (items[i].kind == QA_Q3_ITEM_POWERUP && items[i].tag == (int32_t)powerup) {
                        int32_t seconds =
                            q3_sub_time(player.powerups[powerup], game->now_ms) / 1000;
                        if (seconds < 1)
                            seconds = 1;
                        if (!drop_item(game, actor, (uint32_t)i, yaw, seconds, error))
                            return false;
                        yaw += 45;
                        break;
                    }
            }
        }
    }
    q3_actor *persistent = q3_actor_get(game, player.persistent_item);
    if (persistent && persistent->kind == Q3_ACTOR_ITEM) {
        if (!qa_q3_item_availability(game, persistent->actor, true, 0,
                                     persistent->state.item.expire_at, error))
            return false;
    }
    entry = q3_actor_get(game, actor);
    if (entry && entry->kind == Q3_ACTOR_PLAYER) {
        entry->state.player.persistent = QA_Q3_P_NONE;
        entry->state.player.persistent_item = (qa_actor_id){0};
    }
    return true;
}
static bool player_death_cleanup(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER || entry->state.player.death_cleanup_done)
        return true;
    entry->state.player.death_cleanup_done = true;
    qa_actor_id hook = entry->state.player.hook, mine = entry->state.player.attached_mine;
    if (q3_actor_get(game, hook) &&
        !qa_session_release(game->options.services.session, hook, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    if (q3_actor_get(game, mine) &&
        !qa_session_release(game->options.services.session, mine, error))
        return false;
    if (!q3_actor_get(game, actor))
        return true;
    qa_body_state body;
    if (!qa_world_body_read(game->options.services.world, actor, &body, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    qa_point_query query = {.point = body.origin,
                            .policy = qa_collision_default_policy(QA_COLLISION_Q3)};
    qa_point_contents contents;
    if (!qa_world_point_contents(game->options.services.world, &query, &contents, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    if (!q3_drop_player_items(game, actor, (contents.contents & INT32_MIN) != 0, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry)
        return true;
    entry->state.player.weapon = QA_Q3_W_NONE;
    entry->state.player.loop_sound = 0;
    memset(entry->state.player.powerups, 0, sizeof(entry->state.player.powerups));
    entry->state.player.invulnerability_until = 0;
    if (game->options.product == QA_Q3_TEAM_ARENA &&
        !(entry->state.player.selections & QA_Q3_CHARACTER) &&
        (entry->state.player.flags & 0x200u)) {
        qa_combat_state combat;
        qa_builtin_actor_traits traits = {.gib_health = -40};
        if (!qa_combat_read(game->options.services.combat, actor, &combat, error))
            return false;
        entry = q3_actor_get(game, actor);
        if (!entry || entry->kind != Q3_ACTOR_PLAYER)
            return true;
        if (game->options.services.actor_traits)
            (void)game->options.services.actor_traits(game->options.services.context, actor,
                                                      &traits);
        entry = q3_actor_get(game, actor);
        if (!entry || entry->kind != Q3_ACTOR_PLAYER)
            return true;
        if (combat.health > traits.gib_health &&
            !q3_schedule_kamikaze(game, actor, body.origin, error))
            return false;
    }
    return true;
}
bool qa_q3_player_death_cleanup(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    if (!game || game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "invalid Q3 player death-cleanup boundary");
    ++game->observation_depth;
    bool result = player_death_cleanup(game, actor, error);
    --game->observation_depth;
    return result;
}
bool q3_copy_corpse(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER || entry->state.player.gibbed)
        return true;
    qa_q3_player_state player = entry->state.player;
    qa_body_state body;
    qa_combat_state combat;
    if (!qa_world_body_read(game->options.services.world, actor, &body, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    if (!qa_combat_read_traits(game->options.services.combat, actor, &combat, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    qa_point_query point = {.point = body.origin,
                            .policy = qa_collision_default_policy(QA_COLLISION_Q3)};
    qa_point_contents contents;
    if (!qa_world_point_contents(game->options.services.world, &point, &contents, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    if (contents.contents & INT32_MIN)
        return true;
    uint32_t queue = game->body_queue_index;
    qa_actor_id old = game->body_queue[queue];
    if (q3_actor_get(game, old) && !qa_session_release(game->options.services.session, old, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    qa_actor_collision collision = {.family = QA_COLLISION_Q3,
                                    .shape = QA_SHAPE_BOX,
                                    .contents = INT32_C(0x04000000),
                                    .role = QA_COLLISION_SOLID,
                                    .owner = actor};
    combat.can_take_damage = combat.health > -40;
    combat.armor = (qa_armor){0};
    qa_builtin_spawn spawn = {.owner = game->options.owner,
                              .body = body,
                              .collision = &collision,
                              .combat = &combat,
                              .link = true};
    qa_actor_id corpse;
    if (!qa_builtin_spawn_actor(&game->options.services, &spawn, &corpse, error))
        return false;
    if (!qa_actors_get(qa_session_actors(game->options.services.session), corpse))
        return true;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return qa_session_release(game->options.services.session, corpse, error);
    int32_t animation = player.legs_animation & ~128;
    animation = animation < 2 ? 1 : animation < 4 ? 3 : 5;
    game->actors[corpse.slot] = (q3_actor){
        .actor = corpse,
        .kind = Q3_ACTOR_CORPSE,
        .alpha = 1,
        .state.corpse = {.player = actor,
                         .animation = animation,
                         .timestamp = game->now_ms,
                         .next_sink = q3_add_time(game->now_ms, 5000),
                         .flags = 1u | (player.flags & 0x200u),
                         .trajectory = {.type = player.ground_entity_number >= 0 &&
                                                        player.ground_entity_number != 1023
                                                    ? QA_TRAJECTORY_STATIONARY
                                                    : QA_TRAJECTORY_GRAVITY,
                                        .base = body.origin,
                                        .delta = body.velocity,
                                        .time_ms = game->now_ms}}};
    game->body_queue[queue] = corpse;
    game->body_queue_index = (queue + 1u) % 8u;
    for (uint32_t i = 0; i < game->capacity; ++i) {
        q3_actor *timer = &game->actors[i];
        if (timer->kind == Q3_ACTOR_KAMIKAZE_TIMER &&
            qa_actor_id_equal(timer->state.kamikaze.attacker, actor))
            timer->state.kamikaze.attacker = corpse;
    }
    return true;
}
bool q3_corpse_step(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_CORPSE)
        return true;
    if (entry->state.corpse.flags & 0x80u)
        return true;
    if (q3_sub_time(game->now_ms, entry->state.corpse.timestamp) > 6500) {
        entry->state.corpse.flags |= 0x80u;
        return qa_world_unlink(game->options.services.world, actor, error);
    }
    qa_body_state body;
    if (!qa_world_body_read(game->options.services.world, actor, &body, error))
        return false;
    if (entry->state.corpse.trajectory.type != QA_TRAJECTORY_STATIONARY) {
        qa_vec3 destination;
        if (!qa_trajectory_position(&entry->state.corpse.trajectory, game->now_ms, 800,
                                    &destination, error))
            return false;
        qa_trace_query query = {.start = body.origin,
                                .end = destination,
                                .pass_actor = actor,
                                .shape = {.kind = QA_SHAPE_BOX, .bounds = body.bounds},
                                .policy = qa_collision_default_policy(QA_COLLISION_Q3)};
        query.policy.contents_mask = 0x10001u;
        qa_trace_result trace;
        if (!qa_world_trace(game->options.services.world, &query, &trace, error))
            return false;
        body.origin = trace.end;
        if (trace.fraction < 1) {
            body.velocity = qa_v3(0, 0, 0);
            body.ground = trace.actor;
            entry->state.corpse.trajectory =
                (qa_trajectory){.type = QA_TRAJECTORY_STATIONARY, .base = body.origin};
        }
    }
    if (game->now_ms >= entry->state.corpse.next_sink) {
        entry->state.corpse.next_sink = q3_add_time(game->now_ms, 100);
        entry->state.corpse.trajectory.base.z -= 1;
        body.origin.z -= 1;
    }
    return qa_world_body_write(game->options.services.world, actor, &body, error) &&
           qa_world_link(game->options.services.world, actor, NULL, error);
}
