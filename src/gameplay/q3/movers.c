#include "internal.h"

bool qa_q3_bind_mover(qa_q3_game *game, qa_actor_id actor, const qa_q3_mover_definition *definition,
                      qa_error *error) {
    if (!game || !definition || actor.slot >= game->capacity ||
        !qa_actors_get(qa_session_actors(game->options.services.session), actor) ||
        !qa_vec_finite(definition->first) || !qa_vec_finite(definition->second) ||
        definition->state_index < 0 || definition->state_index > 3)
        return q3_fail(error, "invalid Q3 mover admission");
    q3_actor *entry = &game->actors[actor.slot];
    if (entry->kind && (!qa_actor_id_equal(entry->actor, actor) || entry->kind != Q3_ACTOR_MOVER))
        return q3_fail(error, "actor already has Q3 behavior");
    *entry = (q3_actor){.actor = actor, .kind = Q3_ACTOR_MOVER, .state.mover = *definition};
    return true;
}
static bool mover_read(void *context, qa_actor_id actor, qa_q3_mover_state *out) {
    qa_q3_game *game = context;
    q3_actor *entry = q3_actor_get(game, actor);
    if ((!entry || entry->kind == Q3_ACTOR_PLAYER) && game->options.hooks.foreign_mover_read) {
        if (game->options.hooks.foreign_mover_read(game->options.hooks.context, actor, out))
            return qa_actors_get(qa_session_actors(game->options.services.session), actor) != NULL;
        entry = q3_actor_get(game, actor);
    }
    if (!entry)
        return false;
    if (entry->kind == Q3_ACTOR_MOVER) {
        *out = entry->state.mover.state;
        return true;
    }
    qa_body_state body;
    qa_error ignored = {0};
    if (!qa_world_body_read(game->options.services.world, actor, &body, &ignored))
        return false;
    *out = (qa_q3_mover_state){.kind = QA_Q3_MOVER_NATIVE_FIXED,
                               .position = {.type = QA_TRAJECTORY_STATIONARY, .base = body.origin},
                               .angular = {.type = QA_TRAJECTORY_STATIONARY, .base = body.angles},
                               .ground_entity_number = 1023};
    if (entry->kind == Q3_ACTOR_PLAYER) {
        out->kind = QA_Q3_MOVER_NATIVE_PLAYER;
        out->delta_yaw_word = entry->state.player.delta_yaw_word;
        out->ground_entity_number = entry->state.player.ground_entity_number;
    } else if (entry->kind == Q3_ACTOR_ITEM) {
        out->kind = QA_Q3_MOVER_NATIVE_MOVABLE;
        out->position = entry->state.item.trajectory;
        out->ground_entity_number = entry->state.item.ground_entity_number;
    } else if (entry->kind == Q3_ACTOR_CORPSE) {
        out->kind = QA_Q3_MOVER_NATIVE_MOVABLE;
        out->position = entry->state.corpse.trajectory;
    } else if (entry->kind == Q3_ACTOR_MISSILE && entry->state.missile.weapon == QA_Q3_W_PROX) {
        out->kind = QA_Q3_MOVER_PROXIMITY_MINE;
        out->position = entry->state.missile.trajectory;
        out->proximity_pusher = entry->state.missile.attached;
        out->proximity_direction = entry->state.missile.normal;
    }
    return true;
}
static bool mover_write(void *context, qa_actor_id actor, const qa_q3_mover_state *state,
                        qa_error *error) {
    qa_q3_game *game = context;
    q3_actor *entry = q3_actor_get(game, actor);
    qa_q3_mover_state foreign;
    if ((!entry || entry->kind == Q3_ACTOR_PLAYER) && game->options.hooks.foreign_mover_read) {
        bool selected = game->options.hooks.foreign_mover_read(
            game->options.hooks.context, actor, &foreign);
        if (!qa_actors_get(qa_session_actors(game->options.services.session), actor))
            return true;
        if (selected) {
            if (!game->options.hooks.foreign_mover_write)
                return q3_fail(error, "foreign mover state has no writer");
            return game->options.hooks.foreign_mover_write(game->options.hooks.context, actor,
                                                           state, error);
        }
        entry = q3_actor_get(game, actor);
    }
    if (!entry) {
        if (game->options.hooks.foreign_mover_write)
            return game->options.hooks.foreign_mover_write(game->options.hooks.context, actor,
                                                           state, error);
        return !qa_actors_get(qa_session_actors(game->options.services.session), actor) ||
               q3_fail(error, "foreign mover state has no writer");
    }
    switch (entry->kind) {
    case Q3_ACTOR_MOVER:
        entry->state.mover.state = *state;
        break;
    case Q3_ACTOR_PLAYER:
        entry->state.player.delta_yaw_word = state->delta_yaw_word;
        entry->state.player.ground_entity_number = state->ground_entity_number;
        break;
    case Q3_ACTOR_ITEM:
        entry->state.item.trajectory = state->position;
        entry->state.item.ground_entity_number = state->ground_entity_number;
        break;
    case Q3_ACTOR_CORPSE:
        entry->state.corpse.trajectory = state->position;
        break;
    case Q3_ACTOR_MISSILE:
        entry->state.missile.trajectory = state->position;
        entry->state.missile.attached = state->proximity_pusher;
        entry->state.missile.normal = state->proximity_direction;
        break;
    default:
        break;
    }
    return true;
}
bool q3_mover_set_state(qa_q3_game *game, qa_actor_id actor, int32_t state, int32_t time,
                        qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_MOVER)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(game->options.services.world, actor, &body, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_MOVER)
        return true;
    qa_q3_mover_definition *mover = &entry->state.mover;
    qa_trajectory *position = &mover->state.position;
    mover->state_index = state;
    position->time_ms = time;
    if (position->duration_ms < 1)
        position->duration_ms = 1;
    if (state == 0 || state == 1) {
        position->type = QA_TRAJECTORY_STATIONARY;
        position->base = state == 0 ? mover->first : mover->second;
    } else {
        position->type = QA_TRAJECTORY_LINEAR_STOP;
        position->base = state == 2 ? mover->first : mover->second;
        qa_vec3 delta = state == 2 ? qa_vec_sub(mover->second, mover->first)
                                   : qa_vec_sub(mover->first, mover->second);
        position->delta = qa_vec_scale(delta, 1000.0f / (float)position->duration_ms);
    }
    if (!qa_trajectory_position(position, game->now_ms, 800, &body.origin, error))
        return false;
    if (!qa_world_body_write(game->options.services.world, actor, &body, error))
        return false;
    entry = q3_actor_get(game, actor);
    return !entry || entry->kind != Q3_ACTOR_MOVER ||
           qa_world_link(game->options.services.world, actor, NULL, error);
}
bool q3_mover_match_team(qa_q3_game *game, qa_actor_id leader, int32_t state, int32_t time,
                         qa_error *error) {
    qa_actor_id actor = leader;
    for (uint32_t visited = 0; actor.registry; ++visited) {
        if (visited >= game->capacity)
            return q3_fail(error, "cyclic Q3 mover team");
        q3_actor *entry = q3_actor_get(game, actor);
        if (!entry || entry->kind != Q3_ACTOR_MOVER)
            break;
        qa_actor_id next = entry->state.mover.state.team_next;
        if (!q3_mover_set_state(game, actor, state, time, error))
            return false;
        actor = next;
    }
    return true;
}
bool qa_q3_use_mover(qa_q3_game *game, qa_actor_id actor, qa_actor_id activator, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_MOVER)
        return q3_fail(error, "missing Q3 mover");
    if (entry->state.mover.state.team_slave) {
        qa_actor_id leader = entry->state.mover.team_leader;
        if (!leader.registry || qa_actor_id_equal(leader, actor))
            return q3_fail(error, "Q3 mover slave has no valid leader");
        entry = q3_actor_get(game, leader);
        if (!entry || entry->kind != Q3_ACTOR_MOVER || entry->state.mover.state.team_slave)
            return q3_fail(error, "Q3 mover team leader is invalid");
        actor = leader;
    }
    qa_q3_mover_definition *mover = &entry->state.mover;
    mover->activator = activator;
    if (mover->state_index == 0) {
        if (!q3_mover_match_team(game, actor, 2, q3_add_time(game->now_ms, 50), error))
            return false;
        return q3_map_mover_used(game, actor, 0, 2, error);
    }
    if (mover->state_index == 1) {
        mover->next_think_ms = q3_add_time(game->now_ms, mover->wait_ms);
        return true;
    }
    int32_t total = mover->state.position.duration_ms;
    int32_t partial = q3_sub_time(game->now_ms, mover->state.position.time_ms);
    if (partial > total)
        partial = total;
    int32_t before = mover->state_index;
    int32_t after = before == 3 ? 2 : 3;
    if (!q3_mover_match_team(game, actor, after,
                             q3_sub_time(game->now_ms, q3_sub_time(total, partial)), error))
        return false;
    return q3_map_mover_used(game, actor, before, after, error);
}
static bool mover_action(void *context, qa_q3_mover_action action, qa_actor_id actor,
                         qa_actor_id other, int32_t now, qa_error *error) {
    qa_q3_game *game = context;
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry)
        return true;
    if (action == QA_Q3_MOVER_CRUSH)
        return q3_damage(game, other, actor, actor, QA_Q3_W_NONE, 17, 0, 99999, qa_v3(0, 0, 0),
                         qa_v3(0, 0, 0), false, NULL, error);
    if (action == QA_Q3_MOVER_PROXIMITY_TRIGGER) {
        qa_body_state body;
        if (!qa_world_body_read(game->options.services.world, actor, &body, error))
            return false;
        entry = q3_actor_get(game, actor);
        if (!entry || entry->kind != Q3_ACTOR_MISSILE)
            return true;
        if (!q3_event(game, actor, other, QA_BUILTIN_IMPACT, 67, 0, body.origin, qa_v3(0, 0, 0),
                      qa_v3(0, 0, 0), error))
            return false;
        return !q3_actor_get(game, actor) || q3_missile_explode(game, actor, error);
    }
    if (entry->kind != Q3_ACTOR_MOVER)
        return true;
    qa_q3_mover_definition mover = entry->state.mover;
    if (action == QA_Q3_MOVER_BLOCKED) {
        q3_actor *victim = q3_actor_get(game, other);
        bool player = victim && victim->kind == Q3_ACTOR_PLAYER;
        qa_builtin_actor_traits traits = {0};
        if (!player && game->options.services.actor_traits &&
            game->options.services.actor_traits(game->options.services.context, other,
                                                 &traits))
            player = traits.player;
        if (!q3_actor_get(game, actor) ||
            !qa_actors_get(qa_session_actors(game->options.services.session), other))
            return true;
        victim = q3_actor_get(game, other);
        if (!player && !traits.monster) {
            if (victim && victim->kind == Q3_ACTOR_ITEM) {
                size_t count;
                const qa_q3_item *table = qa_q3_items(game->options.product, &count);
                uint32_t index = victim->state.item.spawn.item_index;
                if (index < count && table[index].kind == QA_Q3_ITEM_TEAM &&
                    game->options.hooks.objective_expired)
                    return game->options.hooks.objective_expired(game->options.hooks.context, other,
                                                                 index, error);
            }
            qa_body_state body;
            if (!qa_world_body_read(game->options.services.world, other, &body, error))
                return false;
            if (!q3_actor_get(game, actor) ||
                !qa_actors_get(qa_session_actors(game->options.services.session), other))
                return true;
            if (!q3_event(game, other, actor, QA_BUILTIN_ITEM, 41, 0, body.origin, qa_v3(0, 0, 0),
                          qa_v3(0, 0, 0), error))
                return false;
            return !qa_actors_get(qa_session_actors(game->options.services.session), other) ||
                   qa_session_release(game->options.services.session, other, error);
        }
        if (mover.damage &&
            !q3_damage(game, other, actor, actor, QA_Q3_W_NONE, 17, 0, (float)mover.damage,
                       qa_v3(0, 0, 0), qa_v3(0, 0, 0), false, NULL, error))
            return false;
        if (!mover.crusher && q3_actor_get(game, actor) &&
            !qa_q3_use_mover(game, actor, other, error))
            return false;
    } else if (mover.map_controlled) {
        bool handled = false;
        if (!q3_map_mover_action(game, action, actor, other, now, &handled, error))
            return false;
        if (!handled)
            return q3_fail(error, "custom Q3 mover action has no map controller");
    } else if (action == QA_Q3_MOVER_REACHED) {
        if (mover.state_index == 2) {
            if (!q3_mover_set_state(game, actor, 1, now, error) ||
                !q3_map_mover_used(game, actor, 2, 1, error))
                return false;
            entry = q3_actor_get(game, actor);
            if (!entry)
                return true;
            entry->state.mover.next_think_ms = q3_add_time(now, mover.wait_ms);
            if (mover.target && game->options.services.use_targets &&
                !game->options.services.use_targets(
                    game->options.services.context, actor,
                    mover.activator.registry ? mover.activator : actor, mover.target, 0, 0, error))
                return false;
        } else if (mover.state_index == 3) {
            if (!q3_mover_set_state(game, actor, 0, now, error) ||
                !q3_map_mover_used(game, actor, 3, 0, error))
                return false;
        }
    } else if (action == QA_Q3_MOVER_THINK && mover.next_think_ms && now >= mover.next_think_ms) {
        entry->state.mover.next_think_ms = 0;
        if (mover.state_index == 1) {
            if (!q3_mover_match_team(game, actor, 3, now, error) ||
                !q3_map_mover_used(game, actor, 1, 3, error))
                return false;
        }
    }
    return !q3_actor_get(game, actor) || !game->options.hooks.mover_action ||
           game->options.hooks.mover_action(game->options.hooks.context, action, actor, other, now,
                                            error);
}
qa_q3_mover_services qa_q3_mover_adapter(qa_q3_game *game) {
    return (qa_q3_mover_services){
        .context = game, .read = mover_read, .write = mover_write, .action = mover_action};
}
