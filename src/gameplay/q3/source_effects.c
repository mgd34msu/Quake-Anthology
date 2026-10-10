#include "client_private.h"

bool q3_source_origins_idle(const qa_q3_game *game) {
    if (!game) return false;
    for (size_t i = 0; i < QA_Q3_SOURCE_CLIENTS; ++i)
        if (game->current_origins[i].active) return false;
    return true;
}
bool q3_source_body_read(qa_q3_game *game, qa_actor_id actor, qa_body_state *out,
                          qa_error *error) {
    if (!game || !out || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "Q3 current shared body needs its real source owner");
    uint32_t slot = UINT32_MAX;
    bool bound = qa_q3_source_actor_slot(game, actor, &slot, NULL);
    if (bound && !game->source_entities[slot].body_attached) {
        *out = (qa_body_state){0};
        return true;
    }
    ++game->observation_depth;
    bool okay = qa_world_body_read(game->options.services.world, actor, out, error);
    uint32_t actual;
    if (okay && (!qa_actors_get(qa_session_actors(game->options.services.session), actor) ||
        (bound && (!qa_q3_source_actor_slot(game, actor, &actual, NULL) || actual != slot))))
        okay = q3_fail(error, "Q3 current shared body retired during observation");
    if (okay && slot < QA_Q3_SOURCE_CLIENTS) {
        const q3_current_origin *current = &game->current_origins[slot];
        if (current->active && qa_actor_id_equal(current->actor, actor)) out->origin = current->origin;
    }
    --game->observation_depth;
    return okay;
}
bool qa_q3_source_current_origin_read(const qa_q3_game *game, qa_actor_id actor,
                                       qa_vec3 *out, qa_error *error) {
    uint32_t slot;
    qa_body_state body;
    if (!out || !qa_q3_source_actor_slot(game, actor, &slot, error) ||
        !q3_source_body_read((qa_q3_game *)game, actor, &body, error)) return false;
    *out = body.origin;
    return true;
}
bool qa_q3_client_current_origin(qa_q3_game *game, qa_actor_id actor, qa_vec3 origin,
                                  qa_error *error) {
    uint32_t slot;
    qa_usercmd command;
    if (!game || !qa_vec_finite(origin) || !q3_client_actor(game, actor, error) ||
        !qa_q3_native_client_slot(game, actor, &slot, error) ||
        !qa_session_active_command(game->options.services.session, game->options.owner, &command) ||
        !qa_actor_id_equal(command.actor, actor))
        return q3_fail(error, "Q3 scoped currentOrigin needs its actual source command");
    if (game->current_origins[slot].active)
        return q3_fail(error, "Q3 currentOrigin source scope is already active");
    game->current_origins[slot] = (q3_current_origin){.actor = actor, .origin = origin, .active = true};
    return true;
}
void q3_source_origin_written(qa_q3_game *game, qa_actor_id actor, qa_vec3 origin) {
    uint32_t slot;
    if (!qa_q3_native_client_slot(game, actor, &slot, NULL)) return;
    q3_current_origin *current = &game->current_origins[slot];
    if (current->active && qa_actor_id_equal(current->actor, actor)) current->origin = origin;
}
bool qa_q3_client_current_origin_finish(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    uint32_t slot;
    if (!q3_client_actor(game, actor, error) ||
        !qa_q3_native_client_slot(game, actor, &slot, error)) return false;
    game->current_origins[slot] = (q3_current_origin){0};
    return true;
}

bool q3_level_state_valid(const qa_q3_game *game, const qa_q3_source_team_state *team,
                          const qa_q3_source_match_state *match, qa_error *error) {
    if (!game || !team || !match || !isfinite(team->last_flag_capture_ms) ||
        !qa_vec_finite(match->intermission_origin) || !qa_vec_finite(match->intermission_angles) ||
        (match->changemap && !qa_strings_cstr(
            qa_session_strings(game->options.services.session), match->changemap)))
        return q3_fail(error, "Q3 source level state has invalid owned values");
    qa_actor_id neutral = team->neutral_obelisk;
    if (!neutral.registry)
        return (!neutral.slot && !neutral.generation) ||
            q3_fail(error, "Q3 source neutral obelisk reference is malformed");
    qa_saved_actor_id saved;
    return qa_actors_save_reference(qa_session_actors(game->options.services.session),
                                    neutral, &saved, error);
}

bool qa_q3_source_team_state_read(const qa_q3_game *game, qa_q3_source_team_state *out,
                                 qa_error *error) {
    if (!game || !out) return q3_fail(error, "Q3 team state needs its source level");
    *out = game->team_state;
    return true;
}
bool qa_q3_source_team_state_write(qa_q3_game *game, const qa_q3_source_team_state *value,
                                  qa_error *error) {
    if (!game || game->source_restored ||
        !q3_level_state_valid(game, value, &game->match_state, error)) return false;
    game->team_state = *value;
    return true;
}
bool qa_q3_source_match_state_read(const qa_q3_game *game, qa_q3_source_match_state *out,
                                  qa_error *error) {
    if (!game || !out) return q3_fail(error, "Q3 match state needs its source level");
    *out = game->match_state;
    return true;
}
bool qa_q3_source_match_state_write(qa_q3_game *game, const qa_q3_source_match_state *value,
                                   qa_error *error) {
    if (!game || game->source_restored ||
        !q3_level_state_valid(game, &game->team_state, value, error)) return false;
    game->match_state = *value;
    return true;
}

bool qa_q3_source_warmup_rebind(qa_q3_game *game, uint64_t copied_revision, bool observed,
                              qa_error *error) {
    if (!game || !game->source_restored || game->observation_depth ||
        !q3_source_origins_idle(game) || !qa_session_safe(game->options.services.session) ||
        !qa_world_idle(game->options.services.world) || !qa_combat_idle(game->options.services.combat))
        return q3_fail(error, "Q3 warmup rebind requires its actual idle cold GAME");
    game->match_state.warmup_modification_count = observed ? copied_revision : copied_revision - 1;
    return true;
}

bool qa_q3_client_award(qa_q3_game *game, qa_actor_id actor, qa_q3_source_award award,
                         int32_t amount, qa_error *error) {
    q3_actor *entry = game && !game->source_restored ? q3_actor_get(game, actor) : NULL;
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return q3_fail(error, "Q3 source award lacks its actual player state");
    qa_q3_player_state *player = &entry->state.player;
    uint32_t slot;
    q3_client_state *client = qa_q3_native_client_slot(game, actor, &slot, NULL)
        ? &game->clients[slot] : NULL;
    int32_t *count;
    switch (award) {
    case QA_Q3_AWARD_IMPRESSIVE: count = &player->impressive_count; break;
    case QA_Q3_AWARD_EXCELLENT: count = &player->excellent_count; break;
    case QA_Q3_AWARD_DEFEND: count = &player->defend_count; break;
    case QA_Q3_AWARD_ASSIST: count = &player->assist_count; break;
    case QA_Q3_AWARD_GAUNTLET: count = &player->gauntlet_frag_count; break;
    case QA_Q3_AWARD_CAPTURE: count = &player->captures; break;
    default: return q3_fail(error, "Q3 source award has no actual persistant slot");
    }
    *count = q3_add_time(*count, amount);
    if (client && client->rule.has_followed_player) client->rule.followed_player.persistant[(uint32_t)award] = *count;
    return true;
}
bool qa_q3_client_connecting(qa_q3_game *game, uint32_t slot, qa_error *error) {
    if (!game || game->source_restored || slot >= QA_Q3_NATIVE_CLIENTS)
        return q3_fail(error, "Q3 connection mutation exceeds its fixed client owner");
    game->clients[slot].rule.connected = QA_Q3_CLIENT_CONNECTING;
    return true;
}
bool qa_q3_client_score_reset(qa_q3_game *game, uint32_t slot, qa_error *error) {
    if (!game || game->source_restored || slot >= QA_Q3_NATIVE_CLIENTS)
        return q3_fail(error, "Q3 source score reset exceeds its fixed client owner");
    qa_q3_player *copied = q3_client_follow_player(game, slot);
    if (copied) copied->persistant[0] = 0;
    game->clients[slot].rule.retired_score = 0;
    return true;
}
static bool move_intermission(qa_q3_game *game, qa_actor_id actor, qa_vec3 origin,
                               qa_vec3 angles, qa_error *error) {
    if (!q3_client_actor(game, actor, error) || !qa_vec_finite(origin) || !qa_vec_finite(angles))
        return q3_fail(error, "Q3 intermission movement needs its actual source client pose");
    qa_body_state body;
    if (!qa_world_body_read(game->options.services.world, actor, &body, error)) return false;
    if (!q3_actor_get(game, actor)) return true;
    body.origin = origin;
    body.angles = angles;
    if (!qa_world_body_write(game->options.services.world, actor, &body, error)) return false;
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry) return true;
    q3_source_origin_written(game, actor, origin);
    qa_q3_player_state *player = &entry->state.player;
    q3_player_view_write(game, actor, angles);
    player->flags = 0;
    memset(player->powerups, 0, sizeof(player->powerups));
    qa_q3_wire_policy policy = {.pm_type = 5};
    if (!qa_q3_wire_player_policy_update(game, actor, QA_Q3_WIRE_PM_TYPE, &policy, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry) return true;
    q3_wire_entity_source *source = q3_wire_entity(game, actor);
    if (!source) return q3_fail(error, "Q3 intermission lost its true source entity");
    source->flags = source->type = source->model = source->loop_sound = source->event = 0;
    source->authored_origin = origin;
    source->authored_angles = angles;
    qa_actor_collision collision;
    if (!qa_world_get_collision(game->options.services.world, actor, &collision, error)) return false;
    collision.contents = (qa_collision_bits){0};
    if (!qa_world_set_collision(game->options.services.world, actor, &collision, error)) return false;
    uint32_t slot;
    if (!qa_q3_native_client_slot(game, actor, &slot, error)) return false;
    qa_q3_player *copied = q3_client_follow_player(game, slot);
    if (copied) { copied->eFlags = 0; memset(copied->powerups, 0, sizeof(copied->powerups)); }
    return !game->options.hooks.source_flags_cleared ||
        game->options.hooks.source_flags_cleared(game->options.hooks.context, actor, error);
}
bool qa_q3_client_move_intermission(qa_q3_game *game, qa_actor_id actor, qa_vec3 origin,
                                    qa_vec3 angles, qa_error *error) {
    if (!game || game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "Q3 intermission movement has no live source owner");
    ++game->observation_depth;
    bool okay = move_intermission(game, actor, origin, angles, error);
    --game->observation_depth;
    return okay;
}

bool qa_q3_source_score_plum(qa_q3_game *game, qa_actor_id actor, qa_vec3 origin,
                             int32_t score, qa_error *error) {
    uint32_t client_slot;
    if (!q3_client_actor(game, actor, error) || !qa_vec_finite(origin) ||
        !qa_q3_native_client_slot(game, actor, &client_slot, error)) return false;
    int32_t recipient = game->source_entities[client_slot].number;
    qa_actor_id temporary;
    if (!q3_wire_temp_entity(game, origin, 65, &temporary, error)) return false;
    if (!qa_actor_id_equal(game->source_entities[client_slot].actor, actor) ||
        !qa_actors_get(qa_session_actors(game->options.services.session), actor)) {
        q3_rollback_spawn(game, temporary, NULL);
        return true;
    }
    qa_q3_entity *source = q3_wire_temporary(game, temporary);
    uint32_t slot;
    if (!source || !qa_q3_source_actor_slot(game, temporary, &slot, error))
        return q3_fail(error, "Q3 score plum lost its actual temporary entity");
    q3_wire_entity_source *fields = q3_wire_entity(game, temporary);
    if (!fields) return q3_fail(error, "Q3 score plum lost its source shared fields");
    game->source_entities[slot].server_flags |= 256u;
    fields->single_client = recipient;
    source->otherEntityNum = recipient;
    source->time = score;
    return true;
}

bool qa_q3_source_team_sound(qa_q3_game *game, qa_vec3 origin, int32_t parameter,
                             qa_error *error) {
    qa_actor_id temporary;
    if (!q3_wire_temp_entity(game, origin, 47, &temporary, error)) return false;
    qa_q3_entity *source = q3_wire_temporary(game, temporary);
    uint32_t slot;
    if (!source || !qa_q3_source_actor_slot(game, temporary, &slot, error))
        return q3_fail(error, "Q3 team sound lost its actual temporary entity");
    source->eventParm = parameter;
    game->source_entities[slot].server_flags |= 32u;
    return true;
}

bool qa_q3_source_team_gesture(qa_q3_game *game, int32_t team, qa_error *error) {
    if (!game || game->source_restored ||
        game->observation_depth == SIZE_MAX)
        return q3_fail(error, "Q3 team gesture needs its actual source session owner");
    ++game->observation_depth;
    for (uint32_t slot = 0; slot < QA_Q3_SOURCE_CLIENTS; ++slot) {
        qa_actor_id actor = game->source_entities[slot].actor;
        if (!game->source_entities[slot].in_use || !actor.registry) continue;
        int32_t actual_team = q3_source_team(game, actor);
        q3_actor *entry = q3_actor_get(game, actor);
        if (actual_team == team && entry && entry->kind == Q3_ACTOR_PLAYER &&
            qa_actor_id_equal(game->source_entities[slot].actor, actor) &&
            game->source_entities[slot].in_use) entry->force_gesture = true;
    }
    --game->observation_depth;
    return true;
}

bool qa_q3_source_award_visual(qa_q3_game *game, qa_actor_id actor, uint32_t flag,
                               qa_error *error) {
    if (!q3_client_actor(game, actor, error)) return false;
    qa_q3_player_state *player = &q3_actor_get(game, actor)->state.player;
    const uint32_t awards = 0x8u | 0x40u | 0x800u | 0x8000u | 0x10000u | 0x20000u;
    player->flags = (player->flags & ~awards) | flag;
    player->reward_until = q3_add_time(game->now_ms, 2000);
    uint32_t slot;
    if (!qa_q3_native_client_slot(game, actor, &slot, error)) return false;
    qa_q3_player *copied = q3_client_follow_player(game, slot);
    if (copied) copied->eFlags = (int32_t)player->flags;
    return true;
}
