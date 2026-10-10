#include "client_private.h"
#include "qa/game_q3_client.h"

bool qa_q3_client_team_state_read(const qa_q3_game *game, uint32_t slot,
                                  qa_q3_source_player_team_state *out, qa_error *error) {
    if (!game || !out || slot >= QA_Q3_NATIVE_CLIENTS)
        return q3_fail(error, "Q3 TEAM telemetry read exceeds its fixed clients");
    *out = game->clients[slot].rule.team;
    return true;
}
bool qa_q3_client_team_state_write(qa_q3_game *game, uint32_t slot,
                                   const qa_q3_source_player_team_state *value, qa_error *error) {
    if (!game || !value || game->source_restored || slot >= QA_Q3_NATIVE_CLIENTS ||
        !isfinite(value->last_hurt_carrier_ms) || !isfinite(value->last_returned_flag_ms) ||
        !isfinite(value->flag_since_ms) || !isfinite(value->last_fragged_carrier_ms))
        return q3_fail(error, "Q3 TEAM telemetry assignment has invalid source state");
    game->clients[slot].rule.team = *value;
    return true;
}
bool qa_q3_client_bot_state_read(const qa_q3_game *game, uint32_t slot,
                                 qa_bot_source_player_state *out, qa_error *error) {
    if (!game || !out || game->source_restored || slot >= QA_Q3_NATIVE_CLIENTS)
        return q3_fail(error, "Q3 bot observation exceeds its fixed clients");
    qa_q3_source_binding binding = game->source_entities[slot];
    qa_bot_source_player_state value = {.present = binding.in_use,
                                    .has_player = binding.client_slot >= 0};
    if (value.has_player) {
        uint32_t client = (uint32_t)binding.client_slot;
        if (client >= QA_Q3_NATIVE_CLIENTS ||
            !qa_q3_wire_client_source_pm_read(game, client, &value.pm_type, error) ||
            !qa_q3_wire_client_source_score_read(game, client, &value.score, error)) return false;
        const qa_q3_player_state *player = &game->client_actors[client].state.player;
        value.last_hurt_client = player->last_hurt_client;
        value.last_hurt_mod = player->last_hurt_mod;
    }
    const qa_q3_source_binding *actual = &game->source_entities[slot];
    if (!qa_actor_id_equal(actual->actor, binding.actor) ||
        actual->client_slot != binding.client_slot || actual->in_use != binding.in_use ||
        actual->body_attached != binding.body_attached)
        return q3_fail(error, "Q3 fixed bot client changed during source observation");
    *out = value;
    return true;
}

bool qa_q3_client_slot_read(const qa_q3_game *game, uint32_t slot,
    qa_q3_native_client *out, qa_error *error) {
    const q3_client_state *client = game ? q3_client_const(game, slot) : NULL;
    if (!client || !out)
        return q3_fail(error, "Q3 client observation exceeds its source slots");
    q3_client_projection(game, client, out);
    return true;
}

bool qa_q3_client_read(const qa_q3_game *game, qa_actor_id actor,
    qa_q3_native_client *out, qa_error *error) {
    uint32_t slot;
    if (!out || !q3_client_slot(game, actor, &slot))
        return q3_fail(error, "Q3 client observation has no actual source actor");
    q3_client_projection(game, q3_client_const(game, slot), out);
    return true;
}

bool qa_q3_client_session_slot_read(const qa_q3_game *game, uint32_t slot,
                                    qa_q3_client_session *out, qa_error *error) {
    if (!game || !out || slot >= QA_Q3_NATIVE_CLIENTS)
        return q3_fail(error, "Q3 session observation exceeds its source clients");
    *out = game->clients[slot].rule.session;
    return true;
}
bool qa_q3_client_session_read(const qa_q3_game *game, qa_actor_id actor,
                               qa_q3_client_session *out, qa_error *error) {
    uint32_t slot;
    return qa_q3_native_client_slot(game, actor, &slot, error) &&
           qa_q3_client_session_slot_read(game, slot, out, error);
}
bool qa_q3_client_session_slot_write(qa_q3_game *game, uint32_t slot, uint32_t mask,
                                     const qa_q3_client_session *value, qa_error *error) {
    if (!game || !value || game->source_restored || slot >= QA_Q3_NATIVE_CLIENTS ||
        (mask & ~(uint32_t)QA_Q3_CLIENT_SESSION_ALL))
        return q3_fail(error, "Q3 session mutation exceeds its source clients");
    qa_q3_client_session *session = &game->clients[slot].rule.session;
    if (mask & QA_Q3_CLIENT_SESSION_TEAM) session->team = value->team;
    if (mask & QA_Q3_CLIENT_SESSION_TIME) session->spectator_time_ms = value->spectator_time_ms;
    if (mask & QA_Q3_CLIENT_SESSION_STATE) session->spectator_state = value->spectator_state;
    if (mask & QA_Q3_CLIENT_SESSION_CLIENT) session->spectator_client = value->spectator_client;
    if (mask & QA_Q3_CLIENT_SESSION_WINS) session->wins = value->wins;
    if (mask & QA_Q3_CLIENT_SESSION_LOSSES) session->losses = value->losses;
    if (mask & QA_Q3_CLIENT_SESSION_LEADER) session->team_leader = value->team_leader;
    qa_actor_player *player = game->clients[slot].player;
    if (player && !game->options.services.player_info)
        player->spectator = session->team == 3;
    return true;
}

bool qa_q3_client_copy_body_queue(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    if (!q3_client_actor(game, actor, error) || game->observation_depth == SIZE_MAX)
        return false;
    ++game->observation_depth;
    bool result = q3_copy_corpse(game, actor, error);
    --game->observation_depth;
    return result;
}

bool qa_q3_client_team_location(qa_q3_game *game, uint32_t slot, int32_t value,
                                qa_error *error) {
    if (!game || game->source_restored || slot >= game->options.max_clients)
        return q3_fail(error, "Q3 team location mutation exceeds its source clients");
    game->clients[slot].rule.team_location = value;
    return true;
}
bool qa_q3_client_team_state(qa_q3_game *game, qa_actor_id actor, int32_t value,
                             qa_error *error) {
    q3_client_state *client = q3_client_actor(game, actor, error);
    if (!client) return false;
    client->rule.team_state = value;
    return true;
}
bool qa_q3_client_ready(qa_q3_game *game, qa_actor_id actor, bool value,
                        qa_error *error) {
    q3_client_state *client = q3_client_actor(game, actor, error);
    if (!client) return false;
    client->rule.ready_to_exit = value;
    return true;
}
bool qa_q3_client_rank(qa_q3_game *game, uint32_t slot, int32_t value, qa_error *error) {
    if (!game || game->source_restored || slot >= QA_Q3_NATIVE_CLIENTS)
        return q3_fail(error, "Q3 rank mutation exceeds its source player states");
    game->client_actors[slot].state.player.rank = value;
    if (game->clients[slot].rule.has_followed_player)
        game->clients[slot].rule.followed_player.persistant[2] = value;
    return true;
}
bool qa_q3_client_persistent_team(qa_q3_game *game, uint32_t slot, int32_t value,
                                  qa_error *error) {
    if (!game || game->source_restored || slot >= QA_Q3_NATIVE_CLIENTS)
        return q3_fail(error, "Q3 PERS_TEAM mutation exceeds its source player states");
    game->client_actors[slot].state.player.persistent_team = value;
    if (game->clients[slot].rule.has_followed_player)
        game->clients[slot].rule.followed_player.persistant[3] = value;
    return true;
}
bool qa_q3_client_tokens_read(const qa_q3_game *game, qa_actor_id actor,
                               int32_t *out, qa_error *error) {
    uint32_t slot;
    if (!out || !qa_q3_native_client_slot(game, actor, &slot, error)) return false;
    *out = game->client_actors[slot].state.player.generic1;
    return true;
}
bool qa_q3_client_tokens_write(qa_q3_game *game, qa_actor_id actor,
                                int32_t value, qa_error *error) {
    if (!q3_client_actor(game, actor, error)) return false;
    q3_actor_get(game, actor)->state.player.generic1 = value;
    uint32_t slot;
    if (!qa_q3_native_client_slot(game, actor, &slot, error)) return false;
    qa_q3_player *copied = q3_client_follow_player(game, slot);
    if (copied) copied->generic1 = value;
    return true;
}

bool q3_followed_player_valid(const qa_q3_game *game, const qa_q3_player *player) {
    if (!game || !player || player->product != game->options.product) return false;
    for (size_t i = 0; i < 3; ++i)
        if (!isfinite(player->origin[i]) || !isfinite(player->velocity[i]) ||
            !isfinite(player->viewangles[i]) || !isfinite(player->grapplePoint[i])) return false;
    return true;
}
bool q3_followed_player_saved_valid(const qa_q3_game *game, const qa_q3_player *player) {
    if (!q3_followed_player_valid(game, player)) return false;
    for (size_t i = 0; i < 3; ++i)
        if (player->origin[i] != 0 || player->velocity[i] != 0) return false;
    for (size_t i = 0; i < 16; ++i)
        if (player->ammo[i] != 0) return false;
    size_t shift = player->product == QA_Q3_TEAM_ARENA ? 1 : 0;
    return player->stats[0] == 0 && player->stats[2 + shift] == 0 &&
        player->stats[3 + shift] == 0;
}
qa_q3_player *q3_client_follow_player(qa_q3_game *game, uint32_t slot) {
    return game && slot < QA_Q3_NATIVE_CLIENTS && game->clients[slot].rule.has_followed_player
        ? &game->clients[slot].rule.followed_player : NULL;
}
bool qa_q3_client_follow_read(const qa_q3_game *game, uint32_t slot,
                              qa_q3_player *out, bool *present, qa_error *error) {
    if (!game || !out || !present || slot >= QA_Q3_NATIVE_CLIENTS)
        return q3_fail(error, "Q3 followed PS observation exceeds its fixed clients");
    *present = game->clients[slot].rule.has_followed_player;
    *out = game->clients[slot].rule.followed_player;
    return true;
}
bool qa_q3_client_follow_clear(qa_q3_game *game, uint32_t slot, qa_error *error) {
    if (!game || game->source_restored || slot >= QA_Q3_NATIVE_CLIENTS)
        return q3_fail(error, "Q3 followed PS reset exceeds its fixed clients");
    game->clients[slot].rule.has_followed_player = false;
    game->clients[slot].rule.followed_player = (qa_q3_player){0};
    return true;
}
bool qa_q3_client_follow_copy(qa_q3_game *game, qa_actor_id actor,
                              const qa_q3_player *source, qa_error *error) {
    q3_client_state *client = q3_client_actor(game, actor, error);
    if (!client || !q3_followed_player_valid(game, source))
        return q3_fail(error, "Q3 followed PS copy lacks genuine source values");
    size_t item_count;
    const qa_q3_item *items = qa_q3_items(game->options.product, &item_count);
    int32_t holdable = source->stats[1];
    int32_t persistent = source->product == QA_Q3_TEAM_ARENA ? source->stats[2] : 0;
    if (holdable < 0 || (size_t)holdable >= item_count ||
        (holdable && items[holdable].kind != QA_Q3_ITEM_HOLDABLE) ||
        persistent < 0 || (size_t)persistent >= item_count ||
        (persistent && items[persistent].kind != QA_Q3_ITEM_PERSISTENT))
        return q3_fail(error, "Q3 followed PS contains invalid source item references");
    uint32_t votes = client->rule.has_followed_player
        ? (uint32_t)client->rule.followed_player.eFlags : q3_actor_get(game, actor)->state.player.flags;
    qa_q3_player copied = *source;
    memset(copied.origin, 0, sizeof(copied.origin));
    memset(copied.velocity, 0, sizeof(copied.velocity));
    copied.stats[0] = 0;
    copied.stats[2 + (copied.product == QA_Q3_TEAM_ARENA ? 1 : 0)] = 0;
    copied.stats[3 + (copied.product == QA_Q3_TEAM_ARENA ? 1 : 0)] = 0;
    memset(copied.ammo, 0, sizeof(copied.ammo));
    copied.eFlags = (int32_t)(((uint32_t)copied.eFlags & ~0x84000u) | (votes & 0x84000u));
    copied.pmFlags = (int32_t)((uint32_t)copied.pmFlags | 4096u);
    qa_q3_wire_policy policy = {.pm_type = copied.pmType, .bob_cycle = copied.bobCycle,
        .pm_flags = copied.pmFlags, .pm_time = copied.pmTime, .gravity = copied.gravity,
        .speed = copied.speed, .movement_dir = copied.movementDir};
    if (!qa_q3_wire_player_policy_update(game, actor, QA_Q3_WIRE_PM_ALL, &policy, error)) return false;
    client = q3_client_actor(game, actor, error);
    if (!client) return false;
    uint32_t slot;
    if (!qa_q3_native_client_slot(game, actor, &slot, error)) return false;
    q3_wire_client_follow_copy(game, slot, &copied);
    client->rule.followed_player = copied;
    client->rule.has_followed_player = true;
    qa_q3_player_state *player = &q3_actor_get(game, actor)->state.player;
    qa_q3_player_motion motion = {.command_time_ms = copied.commandTime,
        .delta_pitch_word = copied.deltaAngles[0], .delta_yaw_word = copied.deltaAngles[1],
        .delta_roll_word = copied.deltaAngles[2], .ground_entity_number = copied.groundEntityNum,
        .view_angles = qa_v3(copied.viewangles[0], copied.viewangles[1], copied.viewangles[2]),
        .view_height = (float)copied.viewheight, .pmove_frame_count = copied.pmoveFramecount,
        .jumppad_frame = copied.jumppadFrame, .jumppad_entity = copied.jumppadEnt};
    q3_player_motion_restore(game, actor, &motion);
    player->flags = (uint32_t)copied.eFlags;
    player->client_number = copied.clientNum;
    player->weapon = (qa_q3_weapon)copied.weapon;
    player->weapon_phase = (qa_q3_weapon_phase)copied.weaponState;
    player->weapon_time_ms = copied.weaponTime;
    player->holdable = holdable ? (qa_q3_holdable)items[holdable].tag : QA_Q3_H_NONE;
    player->persistent = persistent ? (qa_q3_powerup)items[persistent].tag : QA_Q3_P_NONE;
    player->legs_timer_ms = copied.legsTimer; player->legs_animation = copied.legsAnim;
    player->torso_timer_ms = copied.torsoTimer; player->torso_animation = copied.torsoAnim;
    player->grapple_point = qa_v3(copied.grapplePoint[0], copied.grapplePoint[1], copied.grapplePoint[2]);
    memcpy(&player->event_sequence, &copied.eventSequence, sizeof(player->event_sequence));
    memcpy(player->events, copied.events, sizeof(player->events));
    memcpy(player->event_parameters, copied.eventParms, sizeof(player->event_parameters));
    memcpy(&player->entity_event_sequence, &copied.entityEventSequence,
           sizeof(player->entity_event_sequence));
    player->external_event = copied.externalEvent;
    player->external_event_parameter = copied.externalEventParm;
    player->external_event_time = copied.externalEventTime;
    player->damage_event = copied.damageEvent; player->damage_yaw = copied.damageYaw;
    player->damage_pitch = copied.damagePitch; player->damage_count = copied.damageCount;
    memcpy(player->powerups, copied.powerups, sizeof(player->powerups));
    player->generic1 = copied.generic1;
    player->rank = copied.persistant[2]; player->persistent_team = copied.persistant[3];
    player->deaths = copied.persistant[8];
    memcpy(&player->spawn_count, &copied.persistant[4], sizeof(player->spawn_count));
    player->player_events = copied.persistant[5];
    player->max_health = copied.stats[6 + (copied.product == QA_Q3_TEAM_ARENA ? 1 : 0)];
    player->dead_yaw = copied.stats[4 + (copied.product == QA_Q3_TEAM_ARENA ? 1 : 0)];
    player->impressive_count = copied.persistant[9]; player->excellent_count = copied.persistant[10];
    player->defend_count = copied.persistant[11]; player->assist_count = copied.persistant[12];
    player->gauntlet_frag_count = copied.persistant[13]; player->captures = copied.persistant[14];
    return true;
}
bool qa_q3_client_follow_scoreboard(qa_q3_game *game, qa_actor_id actor, bool enabled,
                                    qa_error *error) {
    q3_client_state *client = q3_client_actor(game, actor, error);
    if (!client) return false;
    qa_q3_wire_policy policy;
    if (!qa_q3_wire_player_policy_read(game, actor, &policy, error)) return false;
    policy.pm_flags = enabled ? (int32_t)((uint32_t)policy.pm_flags | 8192u)
                             : (int32_t)((uint32_t)policy.pm_flags & ~8192u);
    if (!qa_q3_wire_player_policy_update(game, actor, QA_Q3_WIRE_PM_FLAGS, &policy, error)) return false;
    client = q3_client_actor(game, actor, error);
    if (!client) return false;
    if (client->rule.has_followed_player) client->rule.followed_player.pmFlags = policy.pm_flags;
    return true;
}
bool qa_q3_client_inactivity_read(const qa_q3_game *game, qa_actor_id actor,
                                  int32_t *deadline, bool *warned, qa_error *error) {
    uint32_t slot;
    if (!deadline || !warned || !qa_q3_native_client_slot(game, actor, &slot, error)) return false;
    *deadline = game->clients[slot].rule.inactivity_time_ms;
    *warned = game->clients[slot].rule.inactivity_warning;
    return true;
}
bool qa_q3_client_inactivity_write(qa_q3_game *game, qa_actor_id actor,
                                   int32_t deadline, bool warned, qa_error *error) {
    q3_client_state *client = q3_client_actor(game, actor, error);
    if (!client) return false;
    client->rule.inactivity_time_ms = deadline;
    client->rule.inactivity_warning = warned;
    return true;
}

bool qa_q3_client_connect(qa_q3_game *game, qa_actor_id actor, bool bot, qa_error *error) {
    q3_client_state *client = q3_client_actor(game, actor, error);
    if (!client) return false;
    uint32_t client_slot;
    if (!q3_client_slot(game, actor, &client_slot)) return false;
    q3_wire_client_clear(game, client_slot);
    *client = (q3_client_state){.rule = {.connected = QA_Q3_CLIENT_CONNECTING,
        .source_model_shape = client->rule.source_model_shape}, .player = client->player};
    if (!client->player->present) client->player->bot = bot;
    q3_actor *entry = q3_actor_get(game, actor);
    uint32_t selections = entry->state.player.selections;
    entry->state.player = (qa_q3_player_state){.selections = selections};
    qa_q3_wire_policy zero = {0};
    if (!qa_q3_wire_player_policy_update(game, actor, QA_Q3_WIRE_PM_ALL, &zero, error)) return false;
    if (!q3_actor_get(game, actor) ||
        !qa_actor_id_equal(game->source_entities[client_slot].actor, actor)) return true;
    if (bot) {
        uint32_t slot;
        if (!q3_client_slot(game, actor, &slot)) return false;
        game->source_entities[slot].server_flags |= 8u;
        game->source_entities[slot].in_use = true;
    }
    q3_player_motion_clear(game, actor);
    uint32_t actual_slot;
    /* A new human's ClientConnect clears PS before G_InitGentity makes its
     * Source row live. ClientBegin publishes that clear after activation. */
    return !qa_q3_native_client_slot(game, actor, &actual_slot, NULL) ||
        !game->source_entities[actual_slot].in_use ||
        !game->options.hooks.source_flags_cleared ||
        game->options.hooks.source_flags_cleared(game->options.hooks.context, actor, error);
}

bool qa_q3_client_begin_state(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    q3_client_state *client = q3_client_actor(game, actor, error);
    if (!client || client->rule.connected == QA_Q3_CLIENT_DISCONNECTED)
        return client ? q3_fail(error, "Q3 ClientBegin requires a connected source client") : false;
    client->rule.connected = QA_Q3_CLIENT_CONNECTED;
    client->rule.enter_time_ms = game->now_ms;
    client->rule.team_state = 0;
    q3_actor *entry = q3_actor_get(game, actor);
    uint32_t slot;
    if (!q3_client_slot(game, actor, &slot)) return false;
    q3_wire_client_clear(game, slot);
    qa_q3_client_follow_clear(game, slot, NULL);
    qa_q3_wire_policy zero = {0};
    if (!qa_q3_wire_player_policy_update(game, actor, QA_Q3_WIRE_PM_ALL, &zero, error)) return false;
    entry = q3_actor_get(game, actor);
    if (!entry || !qa_actor_id_equal(game->source_entities[slot].actor, actor)) return true;
    qa_string_id classname;
    if (!qa_builtin_resource(&game->options.services, "noclass", &classname, error)) return false;
    game->source_entities[slot].classname = classname;
    game->source_entities[slot].in_use = true;
    game->source_entities[slot].client_slot = (int32_t)slot;
    game->source_entities[slot].body_attached = true;
    game->source_entities[slot].number = (int32_t)slot;
    game->source_entities[slot].owner_number = QA_Q3_SOURCE_NONE;
    qa_q3_player_state *player = &entry->state.player;
    /* These are ps fields that ClientBegin clears before ClientSpawn. The
     * latter retains the separate gclient accuracy counters and pers.cmd. */
    player->event_sequence = player->spawn_count = 0;
    memset(player->events, 0, sizeof(player->events));
    memset(player->event_parameters, 0, sizeof(player->event_parameters));
    player->entity_event_sequence = 0;
    player->external_event = player->external_event_parameter = player->external_event_time = 0;
    player->deaths = player->impressive_count = player->excellent_count =
        player->gauntlet_frag_count = player->denied_rewards = player->player_events = 0;
    player->rank = player->persistent_team = player->generic1 = 0;
    player->defend_count = player->assist_count = player->captures = 0;
    player->weapon = player->requested_weapon = QA_Q3_W_NONE;
    player->weapon_phase = QA_Q3_READY;
    player->weapon_time_ms = 0;
    player->selected_pm_flags = 0;
    player->selected_pm_time_ms = 0;
    player->legs_animation = player->torso_animation = 0;
    player->legs_timer_ms = player->torso_timer_ms = 0;
    player->grapple_point = qa_v3(0, 0, 0);
    player->damage_event = player->damage_count = player->damage_pitch = player->damage_yaw = 0;
    memset(player->powerups, 0, sizeof(player->powerups));
    player->holdable = QA_Q3_H_NONE;
    player->max_health = 0;
    player->client_number = 0;
    q3_player_motion_clear(game, actor);
    uint32_t actual_slot;
    return !qa_q3_native_client_slot(game, actor, &actual_slot, NULL) ||
        !game->options.hooks.source_flags_cleared ||
        game->options.hooks.source_flags_cleared(game->options.hooks.context, actor, error);
}

bool qa_q3_client_server_flags(const qa_q3_game *game, uint32_t slot,
    uint32_t *out, qa_error *error) {
    if (!game || !out || slot >= QA_Q3_NATIVE_CLIENTS)
        return q3_fail(error, "Q3 client flags exceed its physical source records");
    *out = game->source_entities[slot].server_flags;
    return true;
}

bool qa_q3_client_set_server_flags(qa_q3_game *game, qa_actor_id actor,
    uint32_t flags, qa_error *error) {
    uint32_t slot;
    if (!q3_client_actor(game, actor, error) || !q3_client_slot(game, actor, &slot)) return false;
    game->source_entities[slot].server_flags = flags;
    return true;
}

bool qa_q3_client_activate_bot(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    uint32_t slot;
    if (!q3_client_actor(game, actor, error) || !q3_client_slot(game, actor, &slot)) return false;
    qa_q3_source_binding *binding = &game->source_entities[slot];
    if (binding->client_slot != (int32_t)slot)
        return q3_fail(error, "Q3 bot activation lost its original fixed client pointer");
    binding->server_flags |= 8u;
    binding->in_use = true;
    return true;
}

bool qa_q3_client_taunt_read(const qa_q3_game *game, uint32_t slot,
    qa_q3_client_taunt *out, qa_error *error) {
    if (!game || !out || slot >= QA_Q3_NATIVE_CLIENTS)
        return q3_fail(error, "Q3 taunt observation exceeds its retained client records");
    const q3_actor *entry = &game->client_actors[slot];
    const qa_q3_player_state *player = &entry->state.player;
    *out = (qa_q3_client_taunt){.enemy = entry->enemy,
        .enemy_source_slot = entry->enemy_source_slot,
        .enemy_source_present = entry->enemy_source_present,
        .last_killed_client = player->last_killed_client,
        .last_hurt_client = player->last_hurt_client,
        .last_hurt_mod = player->last_hurt_mod, .reward_time_ms = player->reward_until};
    return true;
}

bool qa_q3_client_taunt_enemy_clear(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    if (!q3_client_actor(game, actor, error)) return false;
    q3_actor *entry = q3_actor_get(game, actor);
    entry->enemy = (qa_actor_id){0};
    entry->enemy_source_present = false;
    entry->enemy_source_slot = 0;
    return true;
}

bool qa_q3_client_taunt_kill_clear(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    if (!q3_client_actor(game, actor, error)) return false;
    q3_actor_get(game, actor)->state.player.last_killed_client = -1;
    return true;
}

bool qa_q3_client_disconnect(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    q3_client_state *client = q3_client_actor(game, actor, error);
    if (!client) return false;
    uint32_t slot;
    qa_string_id classname;
    if (!q3_client_slot(game, actor, &slot) ||
        !qa_builtin_resource(&game->options.services, "disconnected", &classname, error) ||
        !qa_q3_wire_client_detach(game, slot, error) ||
        !qa_world_unlink(game->options.services.world, actor, error)) return false;
    q3_wire_entity_source *source = q3_wire_entity(game, actor);
    if (!source) return q3_fail(error, "Q3 disconnect lost its actual source entity");
    source->model = 0;
    game->source_entities[slot].in_use = false;
    game->source_entities[slot].classname = classname;
    client->rule.connected = QA_Q3_CLIENT_DISCONNECTED;
    if (!qa_q3_client_persistent_team(game, slot, 0, error)) return false;
    game->source_entities[slot].body_attached = false;
    return true;
}
bool qa_q3_client_disconnect_items(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    if (!game || game->source_restored || game->observation_depth == SIZE_MAX ||
        !q3_client_actor(game, actor, error))
        return q3_fail(error, "Q3 disconnect drops need their actual source client");
    ++game->observation_depth;
    bool okay = q3_drop_player_items(game, actor, false, error);
    --game->observation_depth;
    return okay;
}
bool qa_q3_client_spectator(qa_q3_game *game, qa_actor_id actor,
    bool spectator, qa_error *error) {
    if (!q3_client_actor(game, actor, error)) return false;
    q3_actor_get(game, actor)->state.player.spectator = spectator;
    return true;
}
bool qa_q3_client_stop_following_state(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    uint32_t slot;
    if (!q3_client_actor(game, actor, error) || !q3_client_slot(game, actor, &slot)) return false;
    return qa_q3_client_slot_stop_following_state(game, slot, error);
}
bool qa_q3_client_slot_stop_following_state(qa_q3_game *game, uint32_t slot, qa_error *error) {
    if (!game || game->source_restored || slot >= game->options.max_clients)
        return q3_fail(error, "Q3 StopFollowing exceeds its configured source clients");
    game->source_entities[slot].server_flags &= ~8u;
    qa_q3_player_state *player = &game->client_actors[slot].state.player;
    player->client_number = (int32_t)slot;
    player->spectator = true;
    qa_q3_player *copied = q3_client_follow_player(game, slot);
    if (copied) {
        copied->clientNum = (int32_t)slot;
        copied->pmFlags = (int32_t)((uint32_t)copied->pmFlags & ~4096u);
    }
    return true;
}
bool qa_q3_client_player_flags_update(qa_q3_game *game, uint32_t slot,
    uint32_t set_bits, uint32_t clear_bits, qa_error *error) {
    if (!game || game->source_restored || slot >= game->options.max_clients)
        return q3_fail(error, "Q3 player flags exceed its configured source clients");
    qa_q3_player_state *player = &game->client_actors[slot].state.player;
    player->flags = (player->flags & ~clear_bits) | set_bits;
    if (game->clients[slot].rule.has_followed_player)
        game->clients[slot].rule.followed_player.eFlags = (int32_t)(
            ((uint32_t)game->clients[slot].rule.followed_player.eFlags & ~clear_bits) | set_bits);
    return true;
}

bool qa_q3_client_command(qa_q3_game *game, qa_actor_id actor,
    const qa_q3_usercmd *command, qa_error *error) {
    q3_client_state *client = q3_client_actor(game, actor, error);
    if (!client || !command)
        return client ? q3_fail(error, "Q3 ClientThink lacks its accepted command") : false;
    client->rule.command = *command;
    memcpy(q3_actor_get(game, actor)->state.player.last_command_angles,
           command->angles, sizeof(command->angles));
    return true;
}
bool qa_q3_client_received_command(qa_q3_game *game, qa_actor_id actor,
                                   const qa_q3_usercmd *command, qa_error *error) {
    if (!qa_q3_client_command(game, actor, command, error)) return false;
    q3_actor_get(game, actor)->state.player.last_command_ms = game->now_ms;
    return true;
}
bool qa_q3_client_buttons(qa_q3_game *game, qa_actor_id actor, uint32_t buttons,
                          bool latch, uint32_t *old, qa_error *error) {
    q3_client_state *client = q3_client_actor(game, actor, error);
    if (!client || !old) return client ? q3_fail(error, "Q3 button producer lacks oldButtons output") : false;
    client->rule.old_buttons = client->rule.buttons;
    client->rule.buttons = buttons;
    if (latch) client->rule.latched_buttons |= buttons & ~client->rule.old_buttons;
    *old = client->rule.old_buttons;
    return true;
}
bool qa_q3_client_reward_expire(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    q3_client_state *client = q3_client_actor(game, actor, error);
    if (!client) return false;
    qa_q3_player_state *player = &q3_actor_get(game, actor)->state.player;
    if (game->now_ms > player->reward_until) {
        player->flags &= ~0x38848u;
        if (client->rule.has_followed_player)
            client->rule.followed_player.eFlags = (int32_t)((uint32_t)client->rule.followed_player.eFlags & ~0x38848u);
    }
    return true;
}
bool qa_q3_client_consume_gesture(qa_q3_game *game, qa_actor_id actor, bool *out, qa_error *error) {
    if (!out || !q3_client_actor(game, actor, error)) return false;
    q3_actor *entry = q3_actor_get(game, actor);
    *out = entry->force_gesture;
    entry->force_gesture = false;
    return true;
}
bool qa_q3_client_flag_powerup(qa_q3_game *game, qa_actor_id actor, uint32_t tag,
                              int32_t value, qa_error *error) {
    if (tag < QA_Q3_P_REDFLAG || tag > QA_Q3_P_NEUTRALFLAG || (value && value != INT32_MAX) ||
        !q3_client_actor(game, actor, error))
        return q3_fail(error, "Q3 flag powerup mutation needs its actual source client and flag");
    q3_actor_get(game, actor)->state.player.powerups[tag] = value;
    uint32_t slot;
    if (!qa_q3_native_client_slot(game, actor, &slot, error)) return false;
    qa_q3_player *copied = q3_client_follow_player(game, slot);
    if (copied) copied->powerups[tag] = value;
    return value || !game->options.hooks.source_flags_cleared ||
        game->options.hooks.source_flags_cleared(game->options.hooks.context, actor, error);
}

bool qa_q3_client_command_time(const qa_q3_game *game, qa_actor_id actor,
    int32_t *out, qa_error *error) {
    uint32_t slot;
    if (!out || !q3_client_slot(game, actor, &slot))
        return q3_fail(error, "Q3 command time needs its actual source client");
    if (game->clients[slot].rule.has_followed_player) *out = game->clients[slot].rule.followed_player.commandTime;
    else {
        qa_q3_player_motion motion;
        if (!q3_player_motion_read(game, actor, &motion, error)) return false;
        *out = motion.command_time_ms;
    }
    return true;
}

bool qa_q3_client_think_complete(qa_q3_game *game, qa_actor_id actor,
    int32_t command_time_ms, qa_error *error) {
    if (!q3_client_actor(game, actor, error)) return false;
    q3_player_command_time_write(game, actor, command_time_ms);
    uint32_t slot;
    if (!qa_q3_native_client_slot(game, actor, &slot, error)) return false;
    qa_q3_player *copied = q3_client_follow_player(game, slot);
    if (copied) copied->commandTime = command_time_ms;
    return true;
}

static bool teleport_temporary(qa_q3_game *game, qa_actor_id actor,
    qa_vec3 source_origin, bool entering, qa_actor_id *out, qa_error *error) {
    const q3_actor *player = q3_actor_const(game, actor);
    if (!qa_vec_finite(source_origin))
        return q3_fail(error, "Q3 teleport temporary has an invalid origin");
    uint32_t native_slot;
    int32_t client;
    if (qa_q3_native_client_slot(game, actor, &native_slot, NULL) &&
        player && player->kind == Q3_ACTOR_PLAYER) client = player->state.player.client_number;
    else {
        qa_builtin_player_info info;
        if (!game->options.services.player_info ||
            !game->options.services.player_info(game->options.services.context, actor, &info) ||
            !info.connected || info.slot >= QA_Q3_SOURCE_CLIENTS ||
            !qa_actors_get(qa_session_actors(game->options.services.session), actor))
            return q3_fail(error, "Q3 mixed teleport lacks its actual engine client identity");
        client = (int32_t)info.slot;
    }
    qa_actor_id temporary;
    int32_t event = entering ? 42 : 43;
    if (!q3_wire_temp_entity(game, source_origin, event, &temporary, error)) return false;
    qa_q3_entity *source = q3_wire_temporary(game, temporary);
    if (!source) return q3_fail(error, "Q3 teleport lost its actual temporary source entity");
    source->clientNum = client;
    if (out) *out = temporary;
    return true;
}
bool q3_teleport_event_at(qa_q3_game *game, qa_actor_id actor,
    qa_vec3 origin, bool entering, qa_error *error) {
    return teleport_temporary(game, actor, origin, entering, NULL, error);
}
static bool client_teleport_event(qa_q3_game *game, qa_actor_id actor,
    bool entering, qa_error *error) {
    if (!q3_client_actor(game, actor, error)) return false;
    qa_body_state body;
    qa_actor_id temporary;
    if (!qa_world_body_read(game->options.services.world, actor, &body, error) ||
        !teleport_temporary(game, actor, body.origin, entering, &temporary, error)) return false;
    const q3_actor *entry = q3_actor_const(game, temporary);
    return entry && q3_event(game, temporary, actor, QA_BUILTIN_ANIMATION,
        entering ? 42 : 43, entry->state.temporary.entity.clientNum,
        qa_v3(entry->state.temporary.entity.pos.base[0], entry->state.temporary.entity.pos.base[1],
            entry->state.temporary.entity.pos.base[2]), qa_v3(0, 0, 0), qa_v3(0, 0, 0), error);
}
bool qa_q3_client_teleport_event(qa_q3_game *game, qa_actor_id actor,
    bool entering, qa_error *error) {
    if (!game || game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "Q3 teleport event needs its actual active source");
    ++game->observation_depth;
    bool okay = client_teleport_event(game, actor, entering, error);
    --game->observation_depth;
    return okay;
}

bool qa_q3_client_ping(qa_q3_game *game, qa_actor_id actor, int32_t ping, qa_error *error) {
    q3_client_state *client = q3_client_actor(game, actor, error);
    if (!client) return false;
    client->player->ping = ping;
    return true;
}
bool qa_q3_client_initial_spawn(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    q3_client_state *client = q3_client_actor(game, actor, error);
    if (!client) return false;
    client->rule.initial_spawn = true;
    return true;
}

bool qa_q3_client_team_switch_time(qa_q3_game *game, qa_actor_id actor,
    int32_t time_ms, qa_error *error) {
    q3_client_state *client = q3_client_actor(game, actor, error);
    if (!client) return false;
    client->rule.switch_team_time_ms = time_ms;
    return true;
}

bool qa_q3_player_begin_command(qa_q3_game *game, qa_actor_id actor,
    const qa_q3_usercmd *command, qa_error *error) {
    if (!game || !command || game->source_restored)
        return q3_fail(error, "Q3 client command admission needs its actual source");
    uint32_t slot;
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER || !q3_client_slot(game, actor, &slot))
        return q3_fail(error, "Q3 begin command has no admitted native client slot");
    for (size_t i = 0; i < 3; ++i)
        entry->state.player.last_command_angles[i] = command->angles[i];
    q3_client_state *client = q3_client_at(game, slot);
    client->rule.command = *command;
    client->rule.team_state = 1;
    return true;
}
