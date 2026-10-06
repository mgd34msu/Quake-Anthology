#include "map/internal.h"
#include "qa/game_q3_clients.h"

static bool player_timers(qa_q3_game *, qa_actor_id, int32_t, bool, qa_error *);

bool qa_q3_selected_client_effects_read(const qa_q3_game *game, qa_actor_id actor,
    qa_q3_selected_client_effects *out, qa_error *error) {
    const q3_actor *entry = q3_actor_const(game, actor);
    if (!game || game->source_restored || !out || !entry || entry->kind != Q3_ACTOR_PLAYER ||
        !(entry->state.player.selections & (QA_Q3_ARSENAL | QA_Q3_EQUIPMENT)))
        return q3_fail(error, "Selected client effects need their actual arsenal or equipment player");
    const qa_q3_player_state *player = &entry->state.player;
    *out = (qa_q3_selected_client_effects){.teleport_bit = player->flags & 4u,
        .pm_flags = player->selected_pm_flags, .pm_time_ms = player->selected_pm_time_ms,
        .view_angles = player->view_angles,
        .delta_angle_words = {player->delta_pitch_word, player->delta_yaw_word, player->delta_roll_word},
        .invulnerability_time_ms = player->invulnerability_until, .max_health = player->max_health};
    return true;
}

bool qa_q3_selected_client_effects_publish(qa_q3_game *game, qa_actor_id actor,
    const qa_q3_selected_client_effects *before, qa_error *error) {
    qa_q3_selected_client_effects after;
    if (!before || !qa_q3_selected_client_effects_read(game, actor, &after, error)) return false;
    bool changed = before->teleport_bit != after.teleport_bit || before->pm_flags != after.pm_flags ||
        before->pm_time_ms != after.pm_time_ms || before->invulnerability_time_ms != after.invulnerability_time_ms ||
        before->max_health != after.max_health || before->view_angles.x != after.view_angles.x ||
        before->view_angles.y != after.view_angles.y || before->view_angles.z != after.view_angles.z;
    for (unsigned i = 0; i < 3; ++i)
        changed |= before->delta_angle_words[i] != after.delta_angle_words[i];
    return !changed || !game->options.hooks.selected_client_effects ||
        game->options.hooks.selected_client_effects(game->options.hooks.context, actor, before, &after, error);
}

static bool source_command_active(const qa_q3_game *game, qa_actor_id actor) {
    qa_source_command command;
    uint32_t slot;
    return qa_session_active_command(game->options.services.session, game->options.owner, &command) &&
        qa_actor_id_equal(command.actor, actor) &&
        qa_q3_native_client_slot(game, actor, &slot, NULL);
}

bool qa_q3_player_notarget(qa_q3_game *game, qa_actor_id actor, bool *enabled, qa_error *error) {
    if (!game || game->source_restored)
        return q3_fail(error, "Q3 player source restoration is pending or unavailable");
    q3_actor *source = q3_actor_get(game, actor);
    if (!source || source->kind != Q3_ACTOR_PLAYER || !enabled)
        return q3_fail(error, "Q3 notarget requires an actual player");
    *enabled = source->state.player.no_target = !source->state.player.no_target;
    return true;
}

static bool activate_holdable(qa_q3_game *game, qa_actor_id actor, qa_q3_holdable expected,
                             bool prediction, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER || expected < QA_Q3_H_NONE ||
        expected > QA_Q3_H_INVULNERABILITY ||
        (game->options.product == QA_Q3_ARENA && expected > QA_Q3_H_MEDKIT))
        return q3_fail(error, "invalid Q3 holdable activation");
    qa_q3_player_state *player = &entry->state.player;
    if (player->cutscene.active || player->holdable != expected)
        return true;
    qa_combat_state combat;
    if (!qa_combat_read(game->options.services.combat, actor, &combat, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    player = &entry->state.player;
    if (player->holdable != expected || player->spectator || combat.health <= 0 ||
        player->respawned ||
        (expected == QA_Q3_H_MEDKIT && combat.health >= (float)player->max_health + 25))
        return true;
    player->use_item_held = true;
    player->holdable = QA_Q3_H_NONE;
    if (!q3_inventory_holdable_changed(game, actor, expected, QA_Q3_H_NONE, error))
        return false;
    if (!q3_actor_get(game, actor))
        return true;
    if (!q3_player_event(game, actor, 24 + (int32_t)expected, 0, error))
        return false;
    return prediction || source_command_active(game, actor) || !q3_actor_get(game, actor) ||
           q3_use_holdable(game, actor, expected, error);
}
bool qa_q3_activate_holdable(qa_q3_game *game, qa_actor_id actor, qa_q3_holdable expected,
                             bool prediction, qa_error *error) {
    if (!game || game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "invalid Q3 holdable action boundary");
    ++game->observation_depth;
    bool okay = activate_holdable(game, actor, expected, prediction, error);
    --game->observation_depth;
    return okay;
}

bool qa_q3_bind_player_begin(qa_q3_game *game, qa_actor_id actor, uint32_t selections,
                             int32_t handicap, qa_q3_player_binding *binding, qa_error *error) {
    if (!game || game->source_restored || actor.slot >= game->capacity ||
        !qa_actors_get(qa_session_actors(game->options.services.session), actor) ||
        (!selections && !qa_q3_native_client_slot(game, actor, &(uint32_t){0}, NULL)) ||
        (selections & ~(uint32_t)QA_Q3_ALL_SELECTIONS) || !binding || binding->token ||
        game->player_binding_tokens[actor.slot])
        return q3_fail(error, "invalid Q3 player admission");
    q3_actor *entry = q3_actor_storage(game, actor);
    if (entry->kind && (!qa_actor_id_equal(entry->actor, actor) || entry->kind != Q3_ACTOR_PLAYER))
        return q3_fail(error, "actor already has another Q3 behavior");
    if (handicap < 1 || handicap > 100)
        handicap = 100;
    if (game->player_binding_serial == UINT64_MAX)
        return q3_fail(error, "Q3 player binding identity exhausted");
    ++game->player_binding_serial;
    game->player_binding_tokens[actor.slot] = game->player_binding_serial;
    ++game->player_binding_count;
    *binding = (qa_q3_player_binding){.actor = actor,
                                      .token = game->player_binding_serial,
                                      .prior_selections = entry->kind
                                                              ? entry->state.player.selections
                                                              : 0,
                                      .selections = selections,
                                      .handicap = handicap,
                                      .created = !entry->kind ||
                                          (entry->kind == Q3_ACTOR_PLAYER &&
                                           !entry->state.player.selections &&
                                           !entry->state.player.handicap)};
    return true;
}
static bool binding_matches(qa_q3_game *game, const qa_q3_player_binding *binding,
                            q3_actor **out, qa_error *error) {
    if (!game || !binding || !binding->actor.registry || !binding->token ||
        binding->actor.slot >= game->capacity ||
        game->player_binding_tokens[binding->actor.slot] != binding->token)
        return q3_fail(error, "invalid Q3 player binding transaction");
    if (!qa_actors_get(qa_session_actors(game->options.services.session), binding->actor))
        return q3_fail(error, "Q3 player binding actor was retired");
    q3_actor *entry = q3_actor_storage(game, binding->actor);
    if ((binding->created && entry->kind &&
         (entry->kind != Q3_ACTOR_PLAYER || entry->state.player.selections)) ||
        (!binding->created &&
         (!qa_actor_id_equal(entry->actor, binding->actor) || entry->kind != Q3_ACTOR_PLAYER ||
          entry->state.player.selections != binding->prior_selections)))
        return q3_fail(error, "Q3 player binding changed during admission");
    *out = entry;
    return true;
}
bool qa_q3_bind_player_validate(qa_q3_game *game, const qa_q3_player_binding *binding,
                                qa_error *error) {
    q3_actor *entry;
    return binding_matches(game, binding, &entry, error);
}
bool qa_q3_bind_player_commit(qa_q3_game *game, qa_q3_player_binding *binding,
                              qa_error *error) {
    q3_actor *entry;
    if (!binding_matches(game, binding, &entry, error))
        return false;
    if (binding->created) {
        qa_actor_id enemy = entry->enemy;
        uint32_t enemy_source_slot = entry->enemy_source_slot;
        bool enemy_source_present = entry->enemy_source_present;
        *entry = (q3_actor){.actor = binding->actor,
                            .enemy = enemy,
                            .enemy_source_slot = enemy_source_slot,
                            .enemy_source_present = enemy_source_present,
                            .kind = Q3_ACTOR_PLAYER,
                            .alpha = 1,
                            .state.player = {.weapon = QA_Q3_W_MACHINEGUN,
                                             .requested_weapon = QA_Q3_W_MACHINEGUN,
                                             .max_health = binding->handicap,
                                             .handicap = binding->handicap,
                                             .view_height = 26,
                                             .legs_animation = 22,
                                             .torso_animation = 11,
                                             .air_out_time = q3_add_time(game->now_ms, 12000),
                                             .drowning_damage = 2,
                                             .respawned = true,
                                             .ground_entity_number = 1023}};
    }
    entry->state.player.selections |= binding->selections;
    game->player_binding_tokens[binding->actor.slot] = 0;
    --game->player_binding_count;
    *binding = (qa_q3_player_binding){0};
    return true;
}
bool qa_q3_bind_player_rollback(qa_q3_game *game, qa_q3_player_binding *binding,
                                qa_error *error) {
    if (!game || !binding || !binding->token || binding->actor.slot >= game->capacity)
        return q3_fail(error, "invalid Q3 player binding rollback");
    uint64_t active = game->player_binding_tokens[binding->actor.slot];
    if (active && active != binding->token)
        return q3_fail(error, "Q3 player binding reservation was replaced");
    if (active == binding->token) {
        game->player_binding_tokens[binding->actor.slot] = 0;
        --game->player_binding_count;
    }
    *binding = (qa_q3_player_binding){0};
    return true;
}
bool qa_q3_bind_player(qa_q3_game *game, qa_actor_id actor, uint32_t selections, int32_t handicap,
                       qa_error *error) {
    qa_q3_player_binding binding = {0};
    return qa_q3_bind_player_begin(game, actor, selections, handicap, &binding, error) &&
           qa_q3_bind_player_commit(game, &binding, error);
}
bool qa_q3_player_read(const qa_q3_game *game, qa_actor_id actor, qa_q3_player_state *out) {
    const q3_actor *entry = q3_actor_const(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER || !out)
        return false;
    *out = entry->state.player;
    return true;
}
bool qa_q3_player_fire_read(const qa_q3_game *game, qa_actor_id actor,
                          qa_q3_fire_stamp *out) {
    const q3_actor *entry = q3_actor_const(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER || !out) return false;
    *out = (qa_q3_fire_stamp){.present = entry->state.player.has_last_fire,
                            .time_ms = entry->state.player.last_fire_ms};
    return true;
}
bool qa_q3_grapple_read(const qa_q3_game *game, qa_actor_id actor, qa_q3_grapple_state *out) {
    const q3_actor *entry = q3_actor_const(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER || !out)
        return false;
    const qa_q3_player_state *player = &entry->state.player;
    *out = (qa_q3_grapple_state){.hook = player->hook,
                                 .point = player->grapple_point,
                                 .active = (player->selected_pm_flags & 0x800u) != 0 &&
                                           q3_actor_const(game, player->hook) != NULL,
                                 .fire_held = player->fire_held};
    return true;
}
static bool release_grapple(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    qa_actor_id hook = entry->state.player.hook;
    entry->state.player.fire_held = entry->state.player.grapple_pull = false;
    entry->state.player.selected_pm_flags &= ~0x800u;
    entry->state.player.hook = (qa_actor_id){0};
    return !q3_actor_get(game, hook) ||
           qa_session_release(game->options.services.session, hook, error);
}
static q3_actor *selected_source_current(qa_q3_game *game, qa_actor_id actor,
    uint32_t slot, qa_error *error) {
    uint32_t current;
    q3_actor *entry = q3_actor_get(game, actor);
    if (entry && entry->kind == Q3_ACTOR_PLAYER &&
        qa_q3_native_client_slot(game, actor, &current, NULL) && current == slot &&
        qa_actors_get(qa_session_actors(game->options.services.session), actor))
        return entry;
    q3_fail(error, "Selected Q3 respawn lost its actual source client");
    return NULL;
}
bool qa_q3_selected_source_respawn(qa_q3_game *game, qa_actor_id actor,
    const qa_q3_selected_source_services *services, qa_error *error) {
    uint32_t slot;
    if (!game || game->source_restored || game->observation_depth == SIZE_MAX ||
        !services || !services->pose ||
        !qa_q3_native_client_slot(game, actor, &slot, error))
        return q3_fail(error, "Selected Q3 respawn requires its actual source player pose");
    qa_q3_selected_source_services callbacks = *services;
    ++game->observation_depth;
    bool okay = release_grapple(game, actor, error);
    q3_actor *entry = okay ? selected_source_current(game, actor, slot, error) : NULL;
    if (!entry) { okay = false; goto done; }
    qa_actor_id held = entry->state.player.persistent_item;
    q3_actor *persistent = q3_actor_get(game, held);
    if (persistent) {
        if (persistent->kind != Q3_ACTOR_ITEM) {
            okay = q3_fail(error, "Selected Q3 persistent powerup lost its actual item owner");
            goto done;
        }
        okay = qa_q3_item_availability(game, held, true, 0,
            persistent->state.item.expire_at, error);
        entry = okay ? selected_source_current(game, actor, slot, error) : NULL;
        if (!entry) { okay = false; goto done; }
    } else if (held.registry && qa_actors_get(qa_session_actors(game->options.services.session), held)) {
        okay = q3_fail(error, "Selected Q3 persistent powerup requires its genuine return owner");
        goto done;
    }
    qa_q3_player_state prior = entry->state.player;
    qa_q3_player_state fresh = {
        .selections = prior.selections,
        .event_sequence = prior.event_sequence, .spawn_count = prior.spawn_count,
        .external_slot = prior.external_slot,
        .weapon = prior.weapon, .requested_weapon = prior.requested_weapon,
        .handicap = prior.handicap, .accuracy_shots = prior.accuracy_shots,
        .accuracy_hits = prior.accuracy_hits, .impressive_count = prior.impressive_count,
        .player_events = prior.player_events, .deaths = prior.deaths,
        .excellent_count = prior.excellent_count, .gauntlet_frag_count = prior.gauntlet_frag_count,
        .client_number = (int32_t)slot, .rank = prior.rank,
        .defend_count = prior.defend_count, .assist_count = prior.assist_count,
        .captures = prior.captures,
        .no_target = prior.no_target
    };
    memcpy(fresh.ammo_regeneration_items, prior.ammo_regeneration_items,
        sizeof(fresh.ammo_regeneration_items));
    entry->state.player = fresh;
    qa_q3_native_client *client = &game->clients[slot];
    client->switch_team_time_ms = 0;
    client->old_buttons = client->buttons = client->latched_buttons = 0;
    client->inactivity_time_ms = 0;
    client->old_origin = qa_v3(0, 0, 0);
    client->followed_player = (qa_q3_player){0};
    client->ready_to_exit = client->inactivity_warning = client->has_followed_player = false;
    q3_wire_selected_client_clear(game, slot);
    for (int weapon = 0; okay && weapon < QA_Q3_WEAPON_COUNT; ++weapon) {
        okay = q3_ammo_timer_store(game, actor, (qa_q3_weapon)weapon, 0, error) &&
            selected_source_current(game, actor, slot, error) != NULL;
    }
    qa_q3_selected_source_pose pose = {0};
    if (okay) {
        okay = callbacks.pose(callbacks.context, actor, &pose, error);
        entry = okay ? selected_source_current(game, actor, slot, error) : NULL;
        if (!entry) { okay = false; goto done; }
        if (!qa_vec_finite(pose.view_angles) || !isfinite(pose.view_height) ||
            pose.max_health < 1 || pose.team < 0 || pose.team > 3) {
            okay = q3_fail(error, "Selected Q3 respawn lost its actual source player pose");
            goto done;
        }
        entry->state.player.max_health = pose.max_health;
        entry->state.player.persistent_team = pose.team;
        entry->state.player.view_height = pose.view_height;
        entry->state.player.view_angles = pose.view_angles;
        entry->state.player.spectator = pose.team == 3;
        entry->state.player.powerups[QA_Q3_P_QUAD] = pose.quad_until_ms;
        entry->state.player.powerups[QA_Q3_P_HASTE] = pose.haste_until_ms;
        game->clients[slot].max_health = pose.max_health;
        game->clients[slot].session.team = pose.team;
    }
done:
    --game->observation_depth;
    return okay;
}
bool qa_q3_release_grapple(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    if (!game || game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "invalid Q3 grapple release boundary");
    ++game->observation_depth;
    bool okay = release_grapple(game, actor, error);
    --game->observation_depth;
    return okay;
}
bool qa_q3_player_set_view(qa_q3_game *game, qa_actor_id actor, qa_vec3 angles, float height,
                           qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER || !qa_vec_finite(angles) || !isfinite(height))
        return q3_fail(error, "invalid Q3 player view");
    entry->state.player.view_angles = angles;
    entry->state.player.view_height = height;
    return true;
}
static int32_t q3_angle_word(float angle) {
    float scaled = ((angle * 65536.0f) / 360.0f);
    return (int32_t)((uint32_t)q3_source_float_to_int(scaled) & 65535u);
}
static void q3_cutscene_movement(qa_movement_state *state, qa_movement_command *command,
                                 const qa_q3_cutscene_state *cutscene) {
    qa_q3_movement_state *movement = &state->data.q3;
    movement->movement_type = 4;
    movement->origin = cutscene->origin;
    movement->velocity = qa_v3(0, 0, 0);
    movement->view_angles = cutscene->angles;
    movement->ground = (qa_movement_ground){0};
    if (!command)
        return;
    const float angles[3] = {cutscene->angles.x, cutscene->angles.y, cutscene->angles.z};
    command->buttons = 0;
    command->forward_move = command->side_move = command->up_move = 0;
    for (size_t i = 0; i < 3; ++i)
        command->angle_words[i] =
            (int32_t)(((uint32_t)q3_angle_word(angles[i]) -
                       (uint32_t)movement->delta_angle_words[i]) &
                      65535u);
}
bool qa_q3_character_cutscene(qa_q3_game *game, qa_actor_id actor, qa_vec3 origin,
                              qa_vec3 angles, qa_vec3 view_offset, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER ||
        !(entry->state.player.selections & QA_Q3_CHARACTER) || !qa_vec_finite(origin) ||
        !qa_vec_finite(angles) || !qa_vec_finite(view_offset))
        return q3_fail(error, "invalid Q3 cutscene player state");
    qa_q3_cutscene_state cutscene = {
        .origin = origin, .angles = angles, .view_offset = view_offset, .active = true};
    entry->state.player.cutscene = cutscene;
    entry->state.player.view_angles = angles;
    entry->state.player.view_height = view_offset.z;
    entry->state.player.ground_entity_number = 1023;
    entry->state.player.noclip = false;
    entry->state.player.gauntlet_contact = false;
    return true;
}
bool qa_q3_character_cutscene_clear(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER ||
        !(entry->state.player.selections & QA_Q3_CHARACTER))
        return q3_fail(error, "invalid Q3 cutscene player clear");
    entry->state.player.cutscene = (qa_q3_cutscene_state){0};
    return true;
}
void q3_force_view(qa_q3_player_state *player, qa_vec3 angles, int32_t lock_ms) {
    player->view_angles = angles;
    player->ground_entity_number = 1023;
    player->delta_pitch_word =
        q3_sub_time(q3_angle_word(angles.x), player->last_command_angles[0]);
    player->delta_yaw_word =
        q3_sub_time(q3_angle_word(angles.y), player->last_command_angles[1]);
    player->delta_roll_word =
        q3_sub_time(q3_angle_word(angles.z), player->last_command_angles[2]);
    ++player->teleport_revision;
    player->teleport_lock_ms = lock_ms;
    player->selected_pm_time_ms = lock_ms;
    if (lock_ms) player->selected_pm_flags |= 0x40u;
}
static bool player_state_valid(const qa_q3_player_state *state, bool source_client) {
    if (!state || state->weapon < 0 || state->weapon >= QA_Q3_WEAPON_COUNT ||
        state->requested_weapon < 0 || state->requested_weapon >= QA_Q3_WEAPON_COUNT ||
        state->weapon_phase < QA_Q3_READY || state->weapon_phase > QA_Q3_FIRING ||
        state->external_slot < QA_Q3_SLOT_ACTIVE ||
        state->external_slot > QA_Q3_SLOT_RESUME_REQUESTED ||
        (state->persistent != QA_Q3_P_NONE &&
         (state->persistent < QA_Q3_P_SCOUT || state->persistent > QA_Q3_P_AMMOREGEN)) ||
        state->holdable < QA_Q3_H_NONE || state->holdable > QA_Q3_H_INVULNERABILITY ||
        state->max_health < (source_client ? 0 : 1) ||
        state->handicap < (source_client ? 0 : 1) || state->handicap > 100 ||
        state->drowning_damage < 0 || state->drowning_damage > 15 ||
        !isfinite(state->fractional_weapon_ms) || state->fractional_weapon_ms < 0 ||
        state->fractional_weapon_ms >= 1 || !qa_vec_finite(state->view_angles) ||
        !qa_vec_finite(state->grapple_point) || !isfinite(state->view_height) ||
        !qa_vec_finite(state->cutscene.origin) || !qa_vec_finite(state->cutscene.angles) ||
        !qa_vec_finite(state->cutscene.view_offset) ||
        (state->cutscene.active && !(state->selections & QA_Q3_CHARACTER)) ||
        !qa_vec_finite(state->damage_from) || !isfinite(state->damage_blood) ||
        !isfinite(state->damage_armor) || !isfinite(state->damage_knockback) ||
        (!source_client && !state->selections) ||
        (state->selections & ~(uint32_t)QA_Q3_ALL_SELECTIONS))
        return false;
    return true;
}
bool q3_player_state_valid(const qa_q3_player_state *state) {
    return player_state_valid(state, false);
}
bool q3_player_state_valid_source_client(const qa_q3_player_state *state,
                                          const qa_q3_native_client *client) {
    return client && player_state_valid(state, true);
}
bool qa_q3_player_restore(qa_q3_game *game, qa_actor_id actor, const qa_q3_player_state *state,
                          qa_error *error) {
    if (!q3_player_state_valid(state))
        return q3_fail(error, "invalid Q3 player checkpoint");
    if (!qa_q3_bind_player(game, actor, state->selections, state->handicap, error))
        return false;
    q3_actor_storage(game, actor)->state.player = *state;
    return true;
}
bool q3_ammo_read(qa_q3_game *game, qa_actor_id actor, qa_q3_weapon weapon, int32_t *out,
                  qa_error *error) {
    if (weapon <= QA_Q3_W_NONE || weapon >= QA_Q3_WEAPON_COUNT) {
        *out = 0;
        return true;
    }
    qa_item_id ammo = game->ammo_items[weapon];
    if (!ammo) {
        *out = -1;
        return true;
    }
    qa_inventory_entry entry;
    if (!qa_inventory_entry_read(game->options.services.inventory, actor, ammo, &entry, error))
        return false;
    if (entry.count < INT32_MIN || entry.count > INT32_MAX)
        return q3_fail(error, "Q3 ammunition exceeds source range");
    *out = (int32_t)entry.count;
    return true;
}
bool q3_owns_weapon(qa_q3_game *game, qa_actor_id actor, qa_q3_weapon weapon) {
    if (weapon <= QA_Q3_W_NONE || weapon >= QA_Q3_WEAPON_COUNT ||
        (game->options.product == QA_Q3_ARENA && weapon > QA_Q3_W_GRAPPLE))
        return false;
    qa_inventory_entry entry;
    qa_error ignored = {0};
    return qa_inventory_entry_read(game->options.services.inventory, actor,
                                   game->weapon_items[weapon], &entry, &ignored) &&
           entry.count > 0;
}
bool q3_add_ammo(qa_q3_game *game, qa_actor_id actor, qa_q3_weapon weapon, int32_t quantity,
                 qa_error *error) {
    if (!game->ammo_items[weapon])
        return true;
    int32_t old;
    if (!q3_ammo_read(game, actor, weapon, &old, error))
        return false;
    int32_t next = q3_add_time(old, quantity);
    if (next > 200)
        next = 200;
    double stored;
    return qa_inventory_adjust(game->options.services.inventory, actor, game->ammo_items[weapon],
                               (double)next - old, &stored, error);
}
bool q3_ammo_timer_store(qa_q3_game *game, qa_actor_id actor, qa_q3_weapon weapon,
                         int32_t value, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER || (unsigned)weapon >= QA_Q3_WEAPON_COUNT)
        return q3_fail(error, "Q3 ammo timer store lost its actual player or weapon");
    entry->state.player.ammo_time_ms[weapon] = value;
    uint32_t slot;
    return !game->options.hooks.source_ammo_timer_stored ||
        !qa_q3_native_client_slot(game, actor, &slot, NULL) ||
        game->options.hooks.source_ammo_timer_stored(game->options.hooks.context,
                                                     actor, weapon, value, error);
}
static bool spawn_player(qa_q3_game *game, qa_actor_id actor, const qa_body_state *spawn,
                        qa_team_id team, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER || !spawn)
        return q3_fail(error, "missing Q3 spawn admission");
    uint32_t source_slot;
    if (qa_q3_source_actor_slot(game, actor, &source_slot, NULL)) {
        qa_string_id classname;
        if (!qa_builtin_resource(&game->options.services, "player", &classname, error)) return false;
        game->source_entities[source_slot].classname = classname;
        game->source_entities[source_slot].in_use = true;
        if (source_slot < QA_Q3_SOURCE_CLIENTS)
            entry->state.player.client_number = (int32_t)source_slot;
    }
    qa_q3_player_state *player = &entry->state.player;
    uint32_t native_slot;
    bool native_client = qa_q3_native_client_slot(game, actor, &native_slot, NULL);
    if (!native_client && player->dead && (player->selections & QA_Q3_CHARACTER) &&
        !q3_copy_corpse(game, actor, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    player = &entry->state.player;
    if (native_client) {
        q3_wire_client_spawn_clear(game, native_slot);
        qa_q3_client_follow_clear(game, native_slot, NULL);
        game->clients[native_slot].inactivity_time_ms = 0;
        game->clients[native_slot].inactivity_warning = false;
        game->clients[native_slot].old_buttons = game->clients[native_slot].buttons =
            game->clients[native_slot].latched_buttons = 0;
        game->clients[native_slot].ready_to_exit = false;
        entry->state.player.persistent_team = game->clients[native_slot].session.team;
        entry->state.player.generic1 = 0;
        if (!qa_q3_wire_player_special_ammo(game, actor, QA_Q3_W_GAUNTLET, -1, error) ||
            !qa_q3_wire_player_special_ammo(game, actor, QA_Q3_W_GRAPPLE, -1, error)) return false;
    }
    entry->force_gesture = false;
    if (player->selections & QA_Q3_CHARACTER) {
        qa_combat_state combat = {
            .health = (float)player->handicap + 25,
            .mass = 200,
            .can_take_damage = true,
            .team = team,
            .armor.regular = {.kind = QA_ARMOR_Q3, .protection.q3_protection = 0.66f}};
        qa_combat_state previous;
        qa_error ignored = {0};
        bool has_combat = qa_combat_read(game->options.services.combat, actor, &previous, &ignored);
        entry = q3_actor_get(game, actor);
        if (!entry || entry->kind != Q3_ACTOR_PLAYER) return true;
        if (has_combat) {
            if (!qa_combat_set_health(game->options.services.combat, actor, combat.health, error))
                return false;
            entry = q3_actor_get(game, actor);
            if (!entry || entry->kind != Q3_ACTOR_PLAYER) return true;
            if (!qa_combat_set_armor(game->options.services.combat, actor, &combat.armor, error))
                return false;
            entry = q3_actor_get(game, actor);
            if (!entry || entry->kind != Q3_ACTOR_PLAYER) return true;
            if (!qa_combat_set_traits(game->options.services.combat, actor, &combat, error))
                return false;
        } else if (!qa_combat_create_actor(game->options.services.combat, actor, &combat, error))
            return false;
        entry = q3_actor_get(game, actor);
        if (!entry || entry->kind != Q3_ACTOR_PLAYER) return true;
        qa_body_state body = *spawn;
        body.bounds = (qa_bounds){qa_v3(-15, -15, -24), qa_v3(15, 15, 32)};
        qa_body_state previous_body;
        bool has_body = qa_world_body_read(game->options.services.world, actor, &previous_body, &ignored);
        entry = q3_actor_get(game, actor);
        if (!entry || entry->kind != Q3_ACTOR_PLAYER) return true;
        if (has_body) {
            if (!qa_world_body_write(game->options.services.world, actor, &body, error))
                return false;
        } else if (!qa_world_body_create(game->options.services.world, actor, &body, error))
            return false;
        entry = q3_actor_get(game, actor);
        if (!entry || entry->kind != Q3_ACTOR_PLAYER) return true;
        qa_actor_collision collision = {.family = QA_COLLISION_Q3,
                                        .shape = QA_SHAPE_BOX,
                                        .contents = Q3_CONTENTS_BODY,
                                        .role = QA_COLLISION_SOLID};
        if (!qa_world_set_collision(game->options.services.world, actor, &collision, error))
            return false;
        entry = q3_actor_get(game, actor);
        if (!entry || entry->kind != Q3_ACTOR_PLAYER) return true;
        player = &entry->state.player;
    }
    if (native_client && !(player->selections & QA_Q3_CHARACTER)) {
        qa_body_state body;
        if (!qa_world_body_read(game->options.services.world, actor, &body, error)) return false;
        entry = q3_actor_get(game, actor);
        if (!entry) return true;
        body.origin = spawn->origin;
        body.velocity = spawn->velocity;
        body.angles = spawn->angles;
        if (!qa_world_body_write(game->options.services.world, actor, &body, error)) return false;
        q3_source_origin_written(game, actor, body.origin);
        entry = q3_actor_get(game, actor);
        if (!entry) return true;
        player = &entry->state.player;
    }
    if (player->selections & QA_Q3_ARSENAL) {
        qa_inventory_entry inventory[2 * (QA_Q3_WEAPON_COUNT - 1)];
        size_t count = q3_inventory_arsenal_entries(game, true, inventory);
        if (!qa_inventory_has(game->options.services.inventory, actor)) {
            if (!qa_inventory_create_actor(game->options.services.inventory, actor, inventory,
                                           count, error))
                return false;
        } else {
            for (size_t i = 0; i < count; ++i) {
                if (!qa_inventory_configure(game->options.services.inventory, actor, &inventory[i],
                                            NULL, NULL, error))
                    return false;
                entry = q3_actor_get(game, actor);
                if (!entry || entry->kind != Q3_ACTOR_PLAYER) return true;
            }
        }
        entry = q3_actor_get(game, actor);
        if (!entry || entry->kind != Q3_ACTOR_PLAYER) return true;
        player = &entry->state.player;
        player->weapon = player->requested_weapon = QA_Q3_W_MACHINEGUN;
        player->weapon_phase = QA_Q3_READY;
        player->weapon_time_ms = 0;
        player->external_slot = QA_Q3_SLOT_ACTIVE;
    }
    player->flags = (player->flags & (4u | 0x4000u | 0x80000u)) ^ 4u;
    player->dead = player->gibbed = player->death_cleanup_done = false;
    player->no_target = player->noclip = false;
    player->respawned = true;
    player->respawn_after = game->now_ms;
    player->max_health = player->handicap;
    player->view_angles = spawn->angles;
    player->view_height = 26;
    player->cutscene = (qa_q3_cutscene_state){0};
    if (!(player->selections & QA_Q3_CHARACTER) && game->options.services.actor_traits) {
        qa_builtin_actor_traits traits = {0};
        bool has_traits = game->options.services.actor_traits(game->options.services.context, actor, &traits);
        entry = q3_actor_get(game, actor);
        if (!entry || entry->kind != Q3_ACTOR_PLAYER) return true;
        player = &entry->state.player;
        if (has_traits) {
            if (isfinite(traits.max_health) && traits.max_health >= 1 &&
                traits.max_health < 2147483648.0f)
                player->max_health = (int32_t)traits.max_health;
            if (isfinite(traits.view_height))
                player->view_height = traits.view_height;
        }
    }
    player->invulnerability_until = 0;
    player->invulnerability_expanded = false;
    player->selected_pm_flags = 0;
    player->selected_pm_time_ms = 0;
    player->last_command_ms = game->now_ms;
    player->command_time_ms = q3_sub_time(game->now_ms, 100);
    player->damage_blood = player->damage_armor = player->damage_knockback = 0;
    player->loop_sound = 0;
    player->damage_count = player->damage_event = player->damage_pitch = player->damage_yaw = 0;
    player->time_residual = player->pain_after = player->reward_until = player->last_kill_ms =
        player->rail_streak = 0;
    player->last_killed_client = -1;
    player->last_hurt_client = player->last_hurt_mod = 0;
    memset(player->events, 0, sizeof(player->events));
    memset(player->event_parameters, 0, sizeof(player->event_parameters));
    player->entity_event_sequence = 0;
    player->external_event = player->external_event_parameter = player->external_event_time = 0;
    player->fractional_weapon_ms = 0;
    player->has_last_fire = false;
    player->last_fire_ms = 0;
    player->gauntlet_contact = player->damage_from_world = player->noclip = false;
    for (int weapon = 0; weapon < QA_Q3_WEAPON_COUNT; ++weapon) {
        if (!q3_ammo_timer_store(game, actor, (qa_q3_weapon)weapon, 0, error)) return false;
        entry = q3_actor_get(game, actor);
        if (!entry || entry->kind != Q3_ACTOR_PLAYER) return true;
    }
    player = &entry->state.player;
    player->legs_animation = 22;
    player->torso_animation = 11;
    player->legs_timer_ms = player->torso_timer_ms = 0;
    player->air_out_time = q3_add_time(game->now_ms, 12000);
    player->drowning_damage = 2;
    memset(player->powerups, 0, sizeof(player->powerups));
    player->persistent = QA_Q3_P_NONE;
    player->holdable = QA_Q3_H_NONE;
    player->hook = player->attached_mine = player->portal = player->persistent_item =
        (qa_actor_id){0};
    player->portal_id = 0;
    player->grapple_pull = player->fire_held = player->use_item_held = false;
    ++player->spawn_count;
    if (!qa_q3_inventory_admit(game, actor, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    player = &entry->state.player;
    if (!(player->selections & QA_Q3_CHARACTER) && !native_client)
        return true;
    q3_force_view(player, spawn->angles, 100);
    if (native_client) {
        player->pmove_frame_count = player->jumppad_entity = player->jumppad_frame = 0;
        qa_q3_wire_policy policy = {.pm_type = player->spectator ? 2 : 0,
            .pm_flags = 0x200 | 0x40, .pm_time = 100};
        if (!qa_q3_wire_player_policy_update(game, actor, QA_Q3_WIRE_PM_ALL, &policy, error) ||
            !q3_source_movement_write(game, actor, QA_Q3_SOURCE_PM_ALL, error)) return false;
    }
    if (!player->spectator && !q3_killbox(game, actor, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    player = &entry->state.player;
    if (player->spectator) {
        if (!qa_world_unlink(game->options.services.world, actor, error))
            return false;
    } else if (!(native_client ? qa_q3_wire_link(game, actor, NULL, error)
                               : qa_world_link(game->options.services.world, actor, NULL, error)))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER) return true;
    if (game->options.services.motion_changed) {
        qa_body_state body;
        if (!qa_world_body_read(game->options.services.world, actor, &body, error))
            return false;
        entry = q3_actor_get(game, actor);
        if (!entry || entry->kind != Q3_ACTOR_PLAYER) return true;
        qa_builtin_motion_change change = {.reason = QA_BUILTIN_MOTION_RESET,
                                           .body = body,
                                           .view_angles = spawn->angles,
                                           .force_view_angles = true,
                                           .hold_ns = UINT64_C(100000000)};
        if (!game->options.services.motion_changed(game->options.services.context, actor, &change,
                                                   error))
            return false;
    }
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    player = &entry->state.player;
    if (native_client && game->options.hooks.source_flags_cleared &&
        !game->options.hooks.source_flags_cleared(game->options.hooks.context, actor, error)) return false;
    return native_client || player->spawn_count <= 1 || q3_player_event(game, actor, 42, 0, error);
}
bool qa_q3_spawn_player(qa_q3_game *game, qa_actor_id actor, const qa_body_state *spawn,
                        qa_team_id team, qa_error *error) {
    if (!game || !spawn || game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "invalid Q3 player spawn boundary");
    qa_body_state captured = *spawn;
    ++game->observation_depth;
    bool result = spawn_player(game, actor, &captured, team, error);
    --game->observation_depth;
    return result;
}
static void torso(qa_q3_player_state *player, int32_t animation, bool dead) {
    if (!dead)
        player->torso_animation = ((player->torso_animation & 128) ^ 128) | animation;
}
static bool map_ammo_regeneration(qa_q3_game *game, qa_actor_id actor, qa_q3_weapon weapon,
                                  qa_item_id ammo, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER || weapon <= QA_Q3_W_GAUNTLET ||
        weapon >= QA_Q3_WEAPON_COUNT || weapon == QA_Q3_W_GRAPPLE)
        return q3_fail(error, "invalid Q3 ammo regeneration mapping");
    if (ammo) {
        qa_inventory_entry selected;
        if (!qa_inventory_entry_read(game->options.services.inventory, actor, ammo, &selected,
                                     error))
            return false;
        entry = q3_actor_get(game, actor);
        if (!entry || entry->kind != Q3_ACTOR_PLAYER)
            return true;
    }
    if (entry->state.player.ammo_regeneration_items[weapon] != ammo) {
        if (!q3_ammo_timer_store(game, actor, weapon, 0, error)) return false;
        entry = q3_actor_get(game, actor);
        if (!entry || entry->kind != Q3_ACTOR_PLAYER) return true;
    }
    entry->state.player.ammo_regeneration_items[weapon] = ammo;
    return true;
}
bool qa_q3_map_ammo_regeneration(qa_q3_game *game, qa_actor_id actor, qa_q3_weapon weapon,
                                 qa_item_id ammo, qa_error *error) {
    if (!game || game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "invalid Q3 ammunition mapping boundary");
    ++game->observation_depth;
    bool okay = map_ammo_regeneration(game, actor, weapon, ammo, error);
    --game->observation_depth;
    return okay;
}
bool qa_q3_set_weapon_slot(qa_q3_game *game, qa_actor_id actor, bool holster, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return q3_fail(error, "missing Q3 arsenal");
    qa_q3_external_slot *slot = &entry->state.player.external_slot;
    if (holster) {
        if (*slot == QA_Q3_SLOT_RESUME_REQUESTED)
            *slot = QA_Q3_SLOT_HOLSTERED;
        else if (*slot == QA_Q3_SLOT_ACTIVE)
            *slot = QA_Q3_SLOT_HOLSTER_REQUESTED;
    } else if (*slot != QA_Q3_SLOT_ACTIVE && *slot != QA_Q3_SLOT_RESUME_REQUESTED) {
        if (*slot != QA_Q3_SLOT_HOLSTERED)
            return q3_fail(error, "Q3 weapon has not completed holstering");
        *slot = QA_Q3_SLOT_RESUME_REQUESTED;
    }
    return true;
}
static bool drop_weapon(qa_q3_game *game, qa_actor_id actor, const qa_q3_arsenal_source *source,
                        qa_error *error) {
    if (!q3_player_event(game, actor, 22, 0, error))
        return false;
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry)
        return true;
    entry->state.player.weapon_phase = QA_Q3_DROPPING;
    entry->state.player.weapon_time_ms = q3_add_time(entry->state.player.weapon_time_ms, 200);
    torso(&entry->state.player, 9, source ? source->health <= 0 : entry->state.player.dead);
    return true;
}
static bool raise_weapon(qa_q3_game *game, qa_actor_id actor, qa_q3_player_state *player,
                         const qa_q3_arsenal_source *source) {
    qa_q3_weapon requested = player->requested_weapon;
    bool owned = q3_owns_weapon(game, actor, requested);
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return false;
    player = &entry->state.player;
    player->weapon = owned ? requested : QA_Q3_W_NONE;
    player->weapon_phase = QA_Q3_RAISING;
    player->weapon_time_ms = q3_add_time(player->weapon_time_ms, 250);
    torso(player, 10, source ? source->health <= 0 : player->dead);
    return true;
}
static bool selected_drop_timers(qa_q3_game *game, qa_actor_id actor,
    qa_q3_player_state *player, int32_t milliseconds, bool *advanced, qa_error *error) {
    if (!advanced || *advanced) return true;
    qa_q3_selected_client_effects before;
    if (!qa_q3_selected_client_effects_read(game, actor, &before, error)) return false;
    *advanced = true;
    if (player->selected_pm_time_ms) {
        if (milliseconds >= player->selected_pm_time_ms) {
            player->selected_pm_flags &= ~0x160u;
            player->selected_pm_time_ms = 0;
        } else player->selected_pm_time_ms -= milliseconds;
    }
    return qa_q3_selected_client_effects_publish(game, actor, &before, error);
}
static bool arsenal_step(qa_q3_game *game, qa_actor_id actor, const qa_q3_controls *command,
                         float elapsed_ms, const qa_q3_arsenal_source *source,
                         int32_t source_milliseconds, bool *advanced, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER || !command || !isfinite(elapsed_ms) ||
        elapsed_ms < 0 || (double)elapsed_ms > (double)INT_MAX - 1024.0)
        return q3_fail(error, "invalid Q3 arsenal command");
    qa_q3_player_state *player = &entry->state.player;
    if (player->cutscene.active)
        return true;
    bool attack = command->attack, use = command->use_holdable;
    qa_combat_state combat;
    if (!qa_combat_read(game->options.services.combat, actor, &combat, error))
        return false;
    if (source) combat.health = source->health;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    player = &entry->state.player;
    if (combat.health > 0 && !attack && !use)
        player->respawned = false;
    if (!attack && !command->grapple_independent) {
        player->fire_held = false;
        qa_actor_id hook = player->hook;
        if (!command->prediction && q3_actor_get(game, hook) &&
            !qa_session_release(game->options.services.session, hook, error))
            return false;
    }
    entry = q3_actor_get(game, actor);
    if (!entry)
        return true;
    player = &entry->state.player;
    if (player->spectator)
        return true;
    if (combat.health <= 0) {
        if (player->selections & QA_Q3_ARSENAL)
            player->weapon = QA_Q3_W_NONE;
        bool forced = game->options.rules.force_respawn_seconds > 0 &&
                      (int64_t)game->now_ms - player->respawn_after >
                          (int64_t)game->options.rules.force_respawn_seconds * 1000;
        if (!command->prediction && !source_command_active(game, actor) &&
            (player->selections & QA_Q3_CHARACTER) &&
            game->now_ms > player->respawn_after && (attack || use || forced) &&
            game->options.hooks.respawn)
            return game->options.hooks.respawn(game->options.hooks.context, actor, error);
        return true;
    }
    if (player->respawned)
        return true;
    if (use && !player->use_item_held) {
        if (!selected_drop_timers(game, actor, player, source_milliseconds, advanced, error)) return false;
        entry = q3_actor_get(game, actor);
        if (!entry || entry->kind != Q3_ACTOR_PLAYER) return true;
        player = &entry->state.player;
        return qa_q3_activate_holdable(game, actor, player->holdable, command->prediction, error);
    }
    if (!use)
        player->use_item_held = false;
    if (!(player->selections & QA_Q3_ARSENAL))
        return true;
    float time = elapsed_ms + player->fractional_weapon_ms;
    int32_t milliseconds = (int32_t)time;
    player->fractional_weapon_ms = time - (float)milliseconds;
    if (player->weapon_time_ms > 0)
        player->weapon_time_ms -= milliseconds;
    if (command->requested_weapon >= 0 && command->requested_weapon < QA_Q3_WEAPON_COUNT)
        player->requested_weapon = command->requested_weapon;
    if (player->external_slot == QA_Q3_SLOT_HOLSTERED)
        return true;
    if (player->external_slot == QA_Q3_SLOT_DROPPING) {
        if (player->weapon_time_ms <= 0)
            player->external_slot = QA_Q3_SLOT_HOLSTERED;
        return true;
    }
    if (player->external_slot == QA_Q3_SLOT_RESUME_REQUESTED) {
        if (player->weapon_time_ms <= 0) {
            if (!raise_weapon(game, actor, player, source))
                return true;
            player->external_slot = QA_Q3_SLOT_ACTIVE;
        }
        return true;
    }
    bool switch_weapon = player->requested_weapon != player->weapon &&
                         q3_owns_weapon(game, actor, player->requested_weapon);
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    player = &entry->state.player;
    if (player->external_slot == QA_Q3_SLOT_HOLSTER_REQUESTED && player->weapon_time_ms <= 0 &&
        (player->weapon_phase == QA_Q3_READY || player->weapon_phase == QA_Q3_FIRING) &&
        !switch_weapon) {
        if (!drop_weapon(game, actor, source, error))
            return false;
        entry = q3_actor_get(game, actor);
        if (entry)
            entry->state.player.external_slot = QA_Q3_SLOT_DROPPING;
        return true;
    }
    if ((player->weapon_time_ms <= 0 || player->weapon_phase != QA_Q3_FIRING) && switch_weapon &&
        player->weapon_phase != QA_Q3_DROPPING)
        if (!drop_weapon(game, actor, source, error))
            return false;
    entry = q3_actor_get(game, actor);
    if (!entry)
        return true;
    player = &entry->state.player;
    if (player->weapon_time_ms > 0)
        return true;
    if (player->weapon_phase == QA_Q3_DROPPING) {
        (void)raise_weapon(game, actor, player, source);
        return true;
    }
    if (player->weapon_phase == QA_Q3_RAISING) {
        player->weapon_phase = QA_Q3_READY;
        torso(player, player->weapon == QA_Q3_W_GAUNTLET ? 12 : 11,
            source ? source->health <= 0 : player->dead);
        return true;
    }
    bool gauntlet_hit = true;
    if (attack && player->weapon == QA_Q3_W_GAUNTLET) {
        if (command->gauntlet_contact_known)
            gauntlet_hit = command->gauntlet_contact;
        else if (command->prediction)
            gauntlet_hit = false;
        else if (!q3_gauntlet(game, actor, &gauntlet_hit, error))
            return false;
    }
    entry = q3_actor_get(game, actor);
    if (!entry)
        return true;
    player = &entry->state.player;
    if (!attack || !gauntlet_hit) {
        player->weapon_time_ms = 0;
        player->weapon_phase = QA_Q3_READY;
        return true;
    }
    torso(player, player->weapon == QA_Q3_W_GAUNTLET ? 8 : 7,
        source ? source->health <= 0 : player->dead);
    player->weapon_phase = QA_Q3_FIRING;
    int32_t ammo;
    if (!q3_ammo_read(game, actor, player->weapon, &ammo, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    player = &entry->state.player;
    if (!ammo) {
        player->weapon_time_ms = q3_add_time(player->weapon_time_ms, 500);
        if (!selected_drop_timers(game, actor, player, source_milliseconds, advanced, error)) return false;
        if (!q3_actor_get(game, actor)) return true;
        return q3_player_event(game, actor, 21, 0, error);
    }
    qa_q3_weapon weapon = player->weapon;
    if (ammo != -1) {
        double stored;
        if (!qa_inventory_adjust(game->options.services.inventory, actor, game->ammo_items[weapon],
                                 -1, &stored, error))
            return false;
    }
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    if (!selected_drop_timers(game, actor, &entry->state.player,
        source_milliseconds, advanced, error)) return false;
    if (!q3_actor_get(game, actor)) return true;
    if (!q3_player_event(game, actor, 23, 0, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    if (!command->prediction && !source_command_active(game, actor) &&
        !qa_q3_fire_weapon(game, actor, weapon, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry)
        return true;
    player = &entry->state.player;
    static const int32_t delay[QA_Q3_WEAPON_COUNT] = {400,  400, 100, 1000, 800,  800, 50,
                                                      1500, 100, 200, 400,  1000, 800, 30};
    int32_t add = delay[weapon];
    if (player->persistent == QA_Q3_P_SCOUT)
        add = (int32_t)((float)add / 1.5f);
    else if (player->persistent == QA_Q3_P_AMMOREGEN || (!source && player->powerups[QA_Q3_P_HASTE]))
        add = (int32_t)((float)add / 1.3f);
    else if (source && !source->firing_delay(source->context, actor, add, &add, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER) return true;
    player = &entry->state.player;
    player->weapon_time_ms = q3_add_time(player->weapon_time_ms, add);
    return true;
}
bool qa_q3_arsenal_step(qa_q3_game *game, qa_actor_id actor, const qa_q3_controls *command,
                        float elapsed_ms, qa_error *error) {
    if (!game || game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "invalid Q3 arsenal action boundary");
    ++game->observation_depth;
    bool okay = arsenal_step(game, actor, command, elapsed_ms, NULL, 0, NULL, error);
    --game->observation_depth;
    return okay;
}
bool qa_q3_arsenal_source_step(qa_q3_game *game, qa_actor_id actor, const qa_q3_controls *command,
    float elapsed_ms, const qa_q3_arsenal_source *source, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!game || game->source_restored || game->observation_depth == SIZE_MAX ||
        !entry || entry->kind != Q3_ACTOR_PLAYER || !(entry->state.player.selections & QA_Q3_ARSENAL) ||
        !command || !source || !source->firing_delay || !qa_vec_finite(source->view_angles) ||
        !isfinite(source->view_height) || !isfinite(source->health) ||
        !isfinite(elapsed_ms) || elapsed_ms < 0 || (double)elapsed_ms > (double)INT_MAX - 1024.0)
        return q3_fail(error, "Selected Q3 source slice requires its actual arsenal and source pose");
    qa_q3_arsenal_source captured = *source;
    ++game->observation_depth;
    qa_q3_player_state *player = &entry->state.player;
    player->view_angles = captured.view_angles;
    player->view_height = captured.view_height;
    player->legs_animation = captured.legs_animation;
    player->torso_animation = captured.torso_animation;
    player->legs_timer_ms = captured.legs_timer_ms;
    player->torso_timer_ms = captured.torso_timer_ms;
    qa_q3_controls controls = *command;
    if (player->requested_weapon != player->weapon)
        controls.requested_weapon = player->requested_weapon;
    int32_t milliseconds = (int32_t)(elapsed_ms + player->fractional_weapon_ms);
    controls.gauntlet_contact_known = true;
    controls.gauntlet_contact = false;
    bool okay = true;
    bool advanced = false;
    if (controls.attack && captured.health > 0 && player->weapon == QA_Q3_W_GAUNTLET)
        okay = q3_gauntlet(game, actor, &controls.gauntlet_contact, error);
    entry = q3_actor_get(game, actor);
    if (okay && entry && entry->kind == Q3_ACTOR_PLAYER &&
        (entry->state.player.selections & QA_Q3_ARSENAL))
        okay = arsenal_step(game, actor, &controls, elapsed_ms, &captured,
            milliseconds, &advanced, error);
    entry = okay ? q3_actor_get(game, actor) : NULL;
    if (entry && entry->kind == Q3_ACTOR_PLAYER &&
        (entry->state.player.selections & QA_Q3_ARSENAL)) {
        okay = selected_drop_timers(game, actor, &entry->state.player, milliseconds, &advanced, error);
        qa_combat_state combat;
        if (okay && q3_actor_get(game, actor))
            okay = qa_combat_read(game->options.services.combat, actor, &combat, error);
        else if (okay) goto source_done;
        if (okay && combat.health > 0 && q3_actor_get(game, actor))
            okay = player_timers(game, actor, milliseconds, true, error);
    }
source_done:
    --game->observation_depth;
    return okay;
}
bool qa_q3_player_command(qa_q3_game *game, qa_actor_id actor, const qa_movement_command *command,
                          float elapsed_ms, qa_error *error) {
    if (!command || command->kind != QA_MOVEMENT_Q3)
        return q3_fail(error, "Q3 command wrapper requires the Q3 input dialect");
    qa_q3_controls controls = {.attack = (command->buttons & 1u) != 0,
                               .use_holdable = (command->buttons & 4u) != 0,
                               .requested_weapon = (qa_q3_weapon)command->weapon};
    return qa_q3_arsenal_step(game, actor, &controls, elapsed_ms, error);
}
static bool gib(qa_q3_game *game, qa_actor_id actor, int32_t killer, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry)
        return true;
    entry->state.player.gibbed = true;
    uint32_t source_slot;
    bool native = qa_q3_native_client_slot(game, actor, &source_slot, NULL);
    if (!native) entry->state.player.flags |= 0x80u;
    if (!q3_cancel_kamikaze_timers(game, actor, error))
        return false;
    if (!q3_actor_get(game, actor))
        return true;
    qa_combat_state state;
    if (!qa_combat_read_traits(game->options.services.combat, actor, &state, error))
        return false;
    state.can_take_damage = false;
    if (native) {
        if (!q3_add_event(game, actor, 64, killer, error)) return false;
        if (!q3_actor_get(game, actor)) return true;
        q3_wire_entity_source *source = q3_wire_entity(game, actor);
        if (!source) return q3_fail(error, "Q3 gib lost its actual source entity");
        source->type = 10;
        qa_actor_collision collision;
        if (!qa_world_get_collision(game->options.services.world, actor, &collision, error)) return false;
        collision.contents = 0;
        return qa_combat_set_traits(game->options.services.combat, actor, &state, error) &&
            qa_world_set_collision(game->options.services.world, actor, &collision, error);
    }
    return qa_combat_set_traits(game->options.services.combat, actor, &state, error) &&
           qa_world_unlink(game->options.services.world, actor, error) &&
           q3_player_event(game, actor, 64, killer, error);
}
bool q3_source_initial_death(qa_q3_game *game, const qa_damage_outcome *outcome,
                             bool *admitted, qa_error *error) {
    if (!game || !outcome || !admitted)
        return q3_fail(error, "Q3 source death needs its committed outcome");
    *admitted = false;
    uint32_t slot;
    if (outcome->stale || outcome->result.reaction != QA_REACTION_DEATH ||
        game->match_state.intermission_time_ms != 0 ||
        !qa_q3_native_client_slot(game, outcome->request.target, &slot, NULL)) return true;
    q3_actor *entry = q3_actor_get(game, outcome->request.target);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER) return true;
    qa_q3_wire_policy policy;
    if (!qa_q3_wire_player_policy_read(game, entry->actor, &policy, error)) return false;
    if (policy.pm_type == 3) return true;
    qa_actor_id actor = entry->actor;
    qa_actor_id hook = entry->state.player.hook;
    if (q3_actor_get(game, hook) && !qa_session_release(game->options.services.session, hook, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || !qa_actor_id_equal(game->source_entities[slot].actor, actor)) return true;
    q3_actor *mine = q3_actor_get(game, entry->state.player.attached_mine);
    q3_wire_entity_source *source = q3_wire_entity(game, actor);
    if (!source) return q3_fail(error, "Q3 source death lost its entity fields");
    if (game->options.product == QA_Q3_TEAM_ARENA &&
        (source->flags & 2) && mine && mine->kind == Q3_ACTOR_MISSILE) {
        entry->state.player.flags &= ~2u;
        mine->state.missile.phase = Q3_MISSILE_PROX_DISCARD;
        q3_postgame_native_think_assigned(game, mine->actor);
        mine->state.missile.think_at = game->now_ms;
    }
    policy.pm_type = 3;
    if (!qa_q3_wire_player_policy_update(game, actor, QA_Q3_WIRE_PM_TYPE, &policy, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || !qa_actor_id_equal(game->source_entities[slot].actor, actor)) return true;
    if (entry->state.player.selections & QA_Q3_CHARACTER)
        game->death_continuations[slot] = (q3_death_continuation){.actor = actor,
            .sequence = outcome->request.attack.sequence, .time_ns = outcome->request.attack.time_ns,
            .weapon_provider = outcome->request.attack.weapon_provider};
    *admitted = true;
    return true;
}

bool q3_source_death_effects(qa_q3_game *game, const qa_damage_outcome *outcome,
                             qa_error *error) {
    qa_actor_id actor = outcome->request.target;
    uint32_t slot;
    if (!qa_q3_native_client_slot(game, actor, &slot, NULL)) return true;
    if (!q3_death_rewards(game, actor, &outcome->request, error)) return false;
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || !qa_actor_id_equal(game->source_entities[slot].actor, actor)) return true;
    if (game->options.hooks.death &&
        !game->options.hooks.death(game->options.hooks.context, actor, &outcome->request, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || !qa_actor_id_equal(game->source_entities[slot].actor, actor)) return true;
    if (game->options.hooks.source_frag_bonuses &&
        !game->options.hooks.source_frag_bonuses(game->options.hooks.context,
            actor, outcome->request.attack.attacker, error)) return false;
    entry = q3_actor_get(game, actor);
    if (!entry || !qa_actor_id_equal(game->source_entities[slot].actor, actor) ||
        !game->source_entities[slot].body_attached) return true;
    if (!qa_q3_player_death_cleanup(game, actor, error)) return false;
    entry = q3_actor_get(game, actor);
    if (!entry || !qa_actor_id_equal(game->source_entities[slot].actor, actor)) return true;
    entry->state.player.respawn_after = q3_add_time(game->now_ms, 1700);
    q3_wire_entity_source *source = q3_wire_entity(game, actor);
    if (!source) return q3_fail(error, "Q3 source death lost its native entity state");
    source->weapon = source->powerups = source->loop_sound = 0;
    return true;
}

static bool take_source_death(qa_q3_game *game, const qa_damage_outcome *outcome,
                              uint32_t slot) {
    q3_death_continuation *pending = &game->death_continuations[slot];
    const qa_attack *attack = &outcome->request.attack;
    if (!qa_actor_id_equal(pending->actor, outcome->request.target) ||
        pending->sequence != attack->sequence || pending->time_ns != attack->time_ns ||
        pending->weapon_provider != attack->weapon_provider) return false;
    *pending = (q3_death_continuation){0};
    return true;
}
bool qa_q3_damage_reaction(qa_q3_game *game, const qa_damage_outcome *outcome, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, outcome->request.target);
    if (!entry || outcome->stale)
        return true;
    bool admitted = false;
    if (!q3_source_initial_death(game, outcome, &admitted, error) ||
        (admitted && (!qa_q3_ranking_death(game, outcome, error) ||
                      !q3_source_death_effects(game, outcome, error)))) return false;
    entry = q3_actor_get(game, outcome->request.target);
    if (!entry) return true;
    if (entry->kind == Q3_ACTOR_PORTAL && outcome->result.reaction == QA_REACTION_DEATH)
        return qa_session_release(game->options.services.session, entry->actor, error);
    if (entry->kind == Q3_ACTOR_CORPSE && outcome->result.reaction == QA_REACTION_DEATH) {
        qa_combat_state combat;
        if (!qa_combat_read_traits(game->options.services.combat, entry->actor, &combat, error))
            return false;
        if (combat.health > -40)
            return true;
        if (!game->options.rules.blood)
            return qa_combat_set_health(game->options.services.combat, entry->actor, -39, error);
        qa_actor_id corpse = entry->actor;
        qa_body_state body;
        if (!q3_source_body_read(game, corpse, &body, error))
            return false;
        uint32_t source_slot;
        bool native = qa_q3_source_actor_slot(game, corpse, &source_slot, NULL);
        if (!native) entry->state.corpse.flags |= 0x80u;
        combat.can_take_damage = false;
        if (!q3_cancel_kamikaze_timers(game, corpse, error))
            return false;
        if (!q3_actor_get(game, corpse))
            return true;
        if (native) {
            if (!q3_add_event(game, corpse, 64, 0, error)) return false;
            q3_wire_entity_source *source = q3_wire_entity(game, corpse);
            if (!source) return q3_fail(error, "Q3 corpse gib lost its actual source entity");
            source->type = 10;
            qa_actor_collision collision;
            if (!qa_world_get_collision(game->options.services.world, corpse, &collision, error))
                return false;
            collision.contents = 0;
            return qa_combat_set_traits(game->options.services.combat, corpse, &combat, error) &&
                qa_world_set_collision(game->options.services.world, corpse, &collision, error);
        }
        return qa_combat_set_traits(game->options.services.combat, corpse, &combat, error) &&
               qa_world_unlink(game->options.services.world, corpse, error) &&
               q3_event(game, corpse, outcome->request.attack.attacker, QA_BUILTIN_DEATH, 64, 0,
                        body.origin, qa_v3(0, 0, 0), qa_v3(0, 0, 0), error);
    }
    if (entry->kind == Q3_ACTOR_MISSILE && outcome->result.reaction == QA_REACTION_DEATH) {
        entry->state.missile.phase = Q3_MISSILE_PROX_TRIGGERED;
        q3_postgame_native_think_assigned(game, entry->actor);
        entry->state.missile.think_at = q3_add_time(game->now_ms, 1);
        return true;
    }
    if (entry->kind != Q3_ACTOR_PLAYER || !(entry->state.player.selections & QA_Q3_CHARACTER))
        return true;
    qa_actor_id actor = entry->actor;
    qa_q3_player_state *player = &entry->state.player;
    qa_combat_state combat;
    if (!qa_combat_read_traits(game->options.services.combat, actor, &combat, error))
        return false;
    if (outcome->result.reaction == QA_REACTION_PAIN)
        return true;
    if (outcome->result.reaction != QA_REACTION_DEATH || player->gibbed)
        return true;
    int32_t killer = q3_entity_number(game, outcome->request.attack.attacker);
    if (killer < 0 || killer >= 64)
        killer = 1022;
    uint32_t native_slot;
    bool native_client = qa_q3_native_client_slot(game, actor, &native_slot, NULL);
    bool initial_source_death = native_client && take_source_death(game, outcome, native_slot);
    if (player->dead && !initial_source_death) {
        if (combat.health <= -40 && game->options.rules.blood)
            return gib(game, actor, killer, error);
        return combat.health > -40 ||
               qa_combat_set_health(game->options.services.combat, actor, -39, error);
    }
    player->dead = true;
    player->flags |= 1u;
    player->respawn_after = q3_add_time(game->now_ms, 1700);
    if (!native_client) player->deaths = q3_add_time(player->deaths, 1);
    qa_actor_id hook = player->hook, mine = player->attached_mine;
    if (!native_client && q3_actor_get(game, hook) &&
        !qa_session_release(game->options.services.session, hook, error))
        return false;
    if (!native_client && q3_actor_get(game, mine) &&
        !qa_session_release(game->options.services.session, mine, error))
        return false;
    if (!q3_actor_get(game, actor))
        return true;
    combat.no_knockback = true;
    if (!qa_combat_set_traits(game->options.services.combat, actor, &combat, error))
        return false;
    qa_body_state body;
    if (!qa_world_body_read(game->options.services.world, actor, &body, error))
        return false;
    body.angles.x = body.angles.z = 0;
    body.bounds.maxs.z = -8;
    qa_actor_collision corpse_collision = {.family = QA_COLLISION_Q3,
                                           .shape = QA_SHAPE_BOX,
                                           .contents = INT32_C(0x04000000),
                                           .role = QA_COLLISION_SOLID};
    if (!native_client && !q3_death_rewards(game, actor, &outcome->request, error))
        return false;
    if (!q3_actor_get(game, actor))
        return true;
    if (!native_client && game->options.hooks.death &&
        !game->options.hooks.death(game->options.hooks.context, actor, &outcome->request, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry)
        return true;
    player = &entry->state.player;
    qa_point_query query = {.point = body.origin,
                            .policy = qa_collision_default_policy(QA_COLLISION_Q3)};
    qa_point_contents contents;
    if (!qa_world_point_contents(game->options.services.world, &query, &contents, error))
        return false;
    if (!native_client && !qa_q3_player_death_cleanup(game, actor, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry)
        return true;
    player = &entry->state.player;
    if (!qa_world_body_read(game->options.services.world, actor, &body, error) ||
        !qa_combat_read_traits(game->options.services.combat, actor, &combat, error))
        return false;
    body.angles.x = body.angles.z = 0;
    body.bounds.maxs.z = -8;
    if (!native_client) {
        player->weapon = QA_Q3_W_NONE;
        memset(player->powerups, 0, sizeof(player->powerups));
        player->loop_sound = 0;
    }
    if (!qa_world_set_collision(game->options.services.world, actor, &corpse_collision, error) ||
        !qa_world_body_write(game->options.services.world, actor, &body, error) ||
        !(native_client ? qa_q3_wire_link(game, actor, NULL, error)
                        : qa_world_link(game->options.services.world, actor, NULL, error)))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry)
        return true;
    player = &entry->state.player;
    bool suicide = outcome->request.attack.cause.kind == QA_CAUSE_Q3 &&
                   outcome->request.attack.cause.source.q3.means_of_death == 20;
    if ((combat.health <= -40 && !(contents.contents & INT32_MIN) && game->options.rules.blood) ||
        suicide)
        return gib(game, actor, killer, error);
    if (combat.health <= -40 &&
        !qa_combat_set_health(game->options.services.combat, actor, -39, error))
        return false;
    int32_t animation = (int32_t)(game->death_animation * 2u),
            event = 57 + (int32_t)game->death_animation;
    game->death_animation = (game->death_animation + 1u) % 3u;
    player->legs_animation = ((player->legs_animation & 128) ^ 128) | animation;
    player->torso_animation = ((player->torso_animation & 128) ^ 128) | animation;
    if (!q3_add_event(game, actor, event, killer, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry)
        return true;
    player = &entry->state.player;
    if (game->options.product == QA_Q3_TEAM_ARENA && (player->flags & 0x200u))
        return q3_schedule_kamikaze(game, actor, body.origin, error);
    return true;
}
static bool player_timers(qa_q3_game *game, qa_actor_id actor, int32_t elapsed,
                          bool selected_equipment, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER || elapsed < 0)
        return q3_fail(error, "invalid Q3 player effects");
    qa_q3_player_state *player = &entry->state.player;
    uint32_t source_client;
    bool native = source_command_active(game, actor) &&
        qa_q3_native_client_slot(game, actor, &source_client, NULL) &&
        game->source_entities[source_client].body_attached;
    bool decay = !selected_equipment && (native || (player->selections & QA_Q3_CHARACTER));
    if (!selected_equipment && !native && !(player->selections & (QA_Q3_EFFECTS | QA_Q3_CHARACTER)))
        return true;
    qa_combat_state combat;
    if (!qa_combat_read(game->options.services.combat, actor, &combat, error))
        return false;
    player->time_residual = q3_add_time(player->time_residual, elapsed);
    while (player->time_residual >= 1000 && (native || combat.health > 0)) {
        player->time_residual -= 1000;
        int32_t maximum = player->max_health;
        int32_t regen = player->persistent == QA_Q3_P_GUARD ? maximum / 2
                        : player->powerups[QA_Q3_P_REGEN]   ? maximum
                                                            : 0;
        float health = combat.health;
        bool regenerated = false;
        if (regen) {
            if (health < (float)regen) {
                health = fminf(health + 15, truncf((float)regen * 1.1f));
                regenerated = true;
            } else if (health < (float)regen * 2) {
                health = fminf(health + 5, (float)regen * 2);
                regenerated = true;
            }
        } else if (decay && health > (float)maximum)
            health -= 1;
        if (!qa_combat_set_health(game->options.services.combat, actor, health, error))
            return false;
        if (regenerated && !q3_add_event(game, actor, 63, 0, error))
            return false;
        entry = q3_actor_get(game, actor);
        if (!entry)
            return true;
        player = &entry->state.player;
        if (!qa_combat_read(game->options.services.combat, actor, &combat, error))
            return false;
        if (decay &&
            combat.armor.regular.points > (float)maximum &&
            !qa_combat_set_regular_points(game->options.services.combat, actor,
                                          (float)combat.armor.regular.points - 1, NULL, error))
            return false;
        entry = q3_actor_get(game, actor);
        if (!entry)
            return true;
        player = &entry->state.player;
        if (!qa_combat_read(game->options.services.combat, actor, &combat, error))
            return false;
    }
    if (player->persistent == QA_Q3_P_AMMOREGEN) {
        if (native && game->options.hooks.source_ammo_regeneration) {
            bool handled = false;
            if (!game->options.hooks.source_ammo_regeneration(game->options.hooks.context,
                                                               actor, elapsed, &handled, error))
                return false;
            entry = q3_actor_get(game, actor);
            if (!entry || entry->kind != Q3_ACTOR_PLAYER || handled) return true;
            player = &entry->state.player;
        }
        static const int32_t maxima[14] = {0, 0, 50, 10, 10, 10, 50, 10, 50, 10, 0, 10, 5, 100};
        static const int32_t increments[14] = {0, 0, 4, 1, 1, 1, 5, 1, 5, 1, 0, 1, 1, 5};
        static const int32_t periods[14] = {0,    0,    1000, 1500, 2000, 1750, 1500,
                                            1750, 1500, 4000, 0,    1250, 2000, 1000};
        for (int weapon = 1; weapon < QA_Q3_WEAPON_COUNT; ++weapon) {
            qa_item_id selected = player->ammo_regeneration_items[weapon];
            if (!periods[weapon] || !selected)
                continue;
            qa_inventory_entry resource;
            if (!qa_inventory_entry_read(game->options.services.inventory, actor, selected,
                                         &resource, error))
                return false;
            if (resource.count < INT32_MIN || resource.count > INT32_MAX)
                return q3_fail(error, "Q3 ammo regeneration count exceeds source range");
            int32_t ammo = (int32_t)resource.count;
            int32_t total = q3_add_time(player->ammo_time_ms[weapon], elapsed);
            if (ammo >= maxima[weapon])
                total = 0;
            if (total >= periods[weapon]) {
                total %= periods[weapon];
                double given;
                int32_t add = increments[weapon];
                if (ammo + add > maxima[weapon])
                    add = maxima[weapon] - ammo;
                if (!qa_inventory_give(game->options.services.inventory, actor, selected, add,
                                       &given, error))
                    return false;
            }
            entry = q3_actor_get(game, actor);
            if (!entry)
                return true;
            player = &entry->state.player;
            if (!q3_ammo_timer_store(game, actor, (qa_q3_weapon)weapon, total, error)) return false;
            entry = q3_actor_get(game, actor);
            if (!entry || entry->kind != Q3_ACTOR_PLAYER) return true;
            player = &entry->state.player;
        }
    }
    return true;
}
bool qa_q3_player_timers(qa_q3_game *game, qa_actor_id actor, int32_t elapsed, qa_error *error) {
    return player_timers(game, actor, elapsed, false, error);
}
static bool player_world_effects(qa_q3_game *game, qa_actor_id actor, int32_t water_level,
                                 int32_t water_type, bool noclip, bool native, qa_error *error) {
    if (water_level < 0 || water_level > 3)
        return q3_fail(error, "invalid Q3 world effects");
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry)
        return true;
    qa_q3_player_state *player = &entry->state.player;
    player->noclip = noclip;
    if (!native && (!(player->selections & QA_Q3_EFFECTS) || player->spectator ||
        game->options.rules.intermission))
        return true;
    qa_combat_state combat;
    if (!qa_combat_read(game->options.services.combat, actor, &combat, error))
        return false;
    if (noclip) {
        player->air_out_time = q3_add_time(game->now_ms, 12000);
        return true;
    }
    bool suit = player->powerups[QA_Q3_P_BATTLESUIT] > game->now_ms;
    if (water_level == 3) {
        if (suit)
            player->air_out_time = q3_add_time(game->now_ms, 10000);
        if (player->air_out_time < game->now_ms) {
            player->air_out_time = q3_add_time(player->air_out_time, 1000);
            if (combat.health > 0) {
                player->drowning_damage += 2;
                if (player->drowning_damage > 15)
                    player->drowning_damage = 15;
                int32_t damage = player->drowning_damage;
                const char *sound = combat.health <= (float)damage ? "*drown.wav"
                                    : (q3_rand(game) & 1u)         ? "sound/player/gurp1.wav"
                                                                   : "sound/player/gurp2.wav";
                if (!q3_sound(game, actor, sound, 3, error))
                    return false;
                entry = q3_actor_get(game, actor);
                if (!entry)
                    return true;
                player = &entry->state.player;
                player->pain_after = q3_add_time(game->now_ms, 200);
                if (!q3_damage(game, actor, (qa_actor_id){0}, (qa_actor_id){0}, QA_Q3_W_NONE, 14, 2,
                               (float)damage, qa_v3(0, 0, 0), qa_v3(0, 0, 0), false, NULL, error))
                    return false;
            }
        }
    } else {
        player->air_out_time = q3_add_time(game->now_ms, 12000);
        player->drowning_damage = 2;
    }
    entry = q3_actor_get(game, actor);
    if (!entry)
        return true;
    player = &entry->state.player;
    if (!qa_combat_read(game->options.services.combat, actor, &combat, error))
        return false;
    if (water_level && (water_type & (8 | 16)) && combat.health > 0 &&
        player->pain_after <= game->now_ms) {
        if (suit)
            return q3_add_event(game, actor, 62, 0, error);
        if ((water_type & 8) && !q3_damage(game, actor, (qa_actor_id){0}, (qa_actor_id){0},
                                           QA_Q3_W_NONE, 16, 0, (float)(30 * water_level),
                                           qa_v3(0, 0, 0), qa_v3(0, 0, 0), false, NULL, error))
            return false;
        if ((water_type & 16) && q3_actor_get(game, actor) &&
            !q3_damage(game, actor, (qa_actor_id){0}, (qa_actor_id){0}, QA_Q3_W_NONE, 15, 0,
                       (float)(10 * water_level), qa_v3(0, 0, 0), qa_v3(0, 0, 0), false, NULL,
                       error))
            return false;
    }
    return true;
}
bool qa_q3_player_effects(qa_q3_game *game, qa_actor_id actor, int32_t elapsed,
                          int32_t water_level, int32_t water_type, bool noclip,
                          qa_error *error) {
    return qa_q3_player_timers(game, actor, elapsed, error) &&
        player_world_effects(game, actor, water_level, water_type, noclip, false, error);
}
bool qa_q3_client_world_effects(qa_q3_game *game, qa_actor_id actor, int32_t water_level,
                               int32_t water_type, qa_error *error) {
    uint32_t slot;
    if (!game || game->source_restored ||
        !qa_q3_native_client_slot(game, actor, &slot, error)) return false;
    return player_world_effects(game, actor, water_level, water_type,
                                game->client_actors[slot].state.player.noclip, true, error);
}
bool qa_q3_movement_environment(qa_q3_game *game, qa_actor_id actor, qa_movement_environment *out,
                                qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER || !out)
        return q3_fail(error, "missing Q3 player environment");
    qa_combat_state combat;
    if (!qa_combat_read(game->options.services.combat, actor, &combat, error))
        return false;
    out->health = combat.health;
    entry = q3_actor_get(game, actor);
    if (!entry)
        return true;
    qa_q3_player_state *p = &entry->state.player;
    out->flight = p->powerups[QA_Q3_P_FLIGHT] != 0;
    out->haste = p->persistent != QA_Q3_P_SCOUT && p->powerups[QA_Q3_P_HASTE] != 0;
    out->invulnerable = p->invulnerability_until > game->now_ms;
    out->speed_multiplier = p->persistent == QA_Q3_P_SCOUT ? 1.5f : 1;
    if (p->cutscene.active) {
        out->has_mode = true;
        out->mode = QA_MOVEMENT_MODE_FREEZE;
    }
    if (out->invulnerable) {
        out->fixed_pose = out->fixed_crouched = true;
        out->pose = (qa_movement_posture){
            .bounds = p->invulnerability_expanded
                          ? (qa_bounds){qa_v3(-42, -42, -42), qa_v3(42, 42, 42)}
                          : (qa_bounds){qa_v3(-15, -15, -24), qa_v3(15, 15, 16)},
            .view_height = 12};
    }
    return true;
}
static bool prepare_movement(qa_q3_game *game, qa_actor_id actor, qa_movement_input *input,
                             bool prepare_weapon, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER || !input ||
        !qa_actor_id_equal(input->actor, actor))
        return q3_fail(error, "invalid Q3 movement input");
    qa_q3_player_state *player = &entry->state.player;
    bool native_command = !input->prediction && source_command_active(game, actor);
    if (!native_command) player->gauntlet_contact = false;
    if (prepare_weapon && !native_command && !player->cutscene.active && !input->prediction &&
        input->command.kind == QA_MOVEMENT_Q3 &&
        player->weapon == QA_Q3_W_GAUNTLET && !(input->command.buttons & 2u) &&
        (input->command.buttons & 1u) && player->weapon_time_ms <= 0) {
        bool hit = false;
        if (!q3_gauntlet(game, actor, &hit, error))
            return false;
        entry = q3_actor_get(game, actor);
        if (!entry)
            return true;
        player = &entry->state.player;
        player->gauntlet_contact = hit;
    }
    if (player->selections & QA_Q3_CHARACTER) {
        input->standing = (qa_movement_posture){.bounds = {qa_v3(-15, -15, -24), qa_v3(15, 15, 32)},
                                                .view_height = 26};
        input->crouched = (qa_movement_posture){.bounds = {qa_v3(-15, -15, -24), qa_v3(15, 15, 16)},
                                                .view_height = 12};
        input->dead = (qa_movement_posture){.bounds = {qa_v3(-15, -15, -24), qa_v3(15, 15, -8)},
                                            .view_height = -16};
    }
    input->invulnerability_bounds = (qa_bounds){qa_v3(-42, -42, -42), qa_v3(42, 42, 42)};
    if (!qa_q3_movement_environment(game, actor, &input->environment, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry)
        return true;
    player = &entry->state.player;
    if (player->cutscene.active && input->state.kind == QA_MOVEMENT_Q3) {
        q3_cutscene_movement(&input->state, &input->command, &player->cutscene);
        input->view_offset = player->cutscene.view_offset;
    }
    return true;
}
bool qa_q3_prepare_movement(qa_q3_game *game, qa_actor_id actor, qa_movement_input *input,
                            qa_error *error) {
    return qa_q3_prepare_movement_selected(game, actor, input, true, error);
}
bool qa_q3_prepare_movement_selected(qa_q3_game *game, qa_actor_id actor,
    qa_movement_input *input, bool prepare_weapon, qa_error *error) {
    if (!game || game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "invalid Q3 movement preparation boundary");
    ++game->observation_depth;
    bool okay = prepare_movement(game, actor, input, prepare_weapon, error);
    --game->observation_depth;
    return okay;
}

bool qa_q3_client_events(qa_q3_game *game, qa_actor_id actor, uint32_t old_sequence,
                         qa_error *error) {
    uint32_t slot;
    if (!game || !source_command_active(game, actor) || game->observation_depth == SIZE_MAX ||
        !qa_q3_native_client_slot(game, actor, &slot, error))
        return q3_fail(error, "Q3 ClientEvents needs its actual source command");
    ++game->observation_depth;
    bool okay = true;
    qa_q3_player_state *player = &game->client_actors[slot].state.player;
    int32_t old, sequence, oldest;
    memcpy(&old, &old_sequence, sizeof(old));
    memcpy(&sequence, &player->event_sequence, sizeof(sequence));
    oldest = q3_sub_time(sequence, 2);
    if (old < oldest) old = oldest;
    for (; old < sequence; old = q3_add_time(old, 1)) {
        if (!qa_actor_id_equal(game->source_entities[slot].actor, actor) ||
            !q3_actor_get(game, actor)) break;
        player = &game->client_actors[slot].state.player;
        int32_t event = player->events[(uint32_t)old & 1u];
        if (event == 11 || event == 12) {
            q3_wire_entity_source *source = q3_wire_entity(game, actor);
            if (source && source->type == 1 && !(game->options.rules.dmflags & 8)) {
                player->pain_after = q3_add_time(game->now_ms, 200);
                okay = q3_damage(game, actor, (qa_actor_id){0}, (qa_actor_id){0},
                    QA_Q3_W_NONE, 19, 2, event == 12 ? 10 : 5,
                    qa_v3(0, 0, 0), qa_v3(0, 0, 0), false, NULL, error);
            }
        } else if (event == 23) {
            if (!game->options.hooks.primary_attack_allowed) {
                okay = q3_fail(error, "Q3 source weapon fire lacks its actual primary selection owner");
            } else {
                bool allowed = game->options.hooks.primary_attack_allowed(
                    game->options.hooks.context, actor);
                if (allowed && qa_actor_id_equal(game->source_entities[slot].actor, actor) &&
                    q3_actor_get(game, actor))
                    okay = qa_q3_fire_weapon(game, actor, game->client_actors[slot].state.player.weapon, error);
            }
        }
        else if (event >= 25 && event <= 29)
            okay = q3_use_holdable(game, actor, (qa_q3_holdable)(event - 24), error);
        if (!okay) break;
        player = &game->client_actors[slot].state.player;
        memcpy(&sequence, &player->event_sequence, sizeof(sequence));
    }
    --game->observation_depth;
    return okay;
}

bool qa_q3_client_think_event_time(qa_q3_game *game, qa_actor_id actor,
                                   uint32_t old_sequence, qa_error *error) {
    uint32_t slot;
    if (!game || !source_command_active(game, actor) ||
        !qa_q3_native_client_slot(game, actor, &slot, error)) return false;
    return game->client_actors[slot].state.player.event_sequence == old_sequence ||
        q3_wire_event_time(game, actor, game->now_ms, error);
}
bool qa_q3_client_fire_held_finish(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    uint32_t slot;
    if (!game || !source_command_active(game, actor) ||
        !qa_q3_native_client_slot(game, actor, &slot, error)) return false;
    if (!(game->client_actors[slot].state.player.flags & 0x100u))
        game->client_actors[slot].state.player.fire_held = false;
    return true;
}
bool qa_q3_client_movement_complete(qa_q3_game *game, qa_actor_id actor, int32_t time,
                                     qa_vec3 view, float height, qa_movement_ground ground,
                                     qa_error *error) {
    uint32_t slot;
    if (!game || !source_command_active(game, actor) || !qa_vec_finite(view) || !isfinite(height) ||
        !qa_q3_native_client_slot(game, actor, &slot, error)) return false;
    qa_q3_player_state *player = &game->client_actors[slot].state.player;
    player->command_time_ms = time;
    player->view_angles = view;
    player->view_height = height;
    uint32_t source_slot;
    player->ground_entity_number = ground.hit == QA_TRACE_HIT_WORLD ? (int32_t)QA_Q3_SOURCE_WORLD :
        ground.hit == QA_TRACE_HIT_ACTOR && qa_q3_source_actor_slot(game, ground.actor, &source_slot, NULL)
        ? (int32_t)source_slot : (int32_t)QA_Q3_SOURCE_NONE;
    qa_q3_native_client *client = &game->clients[slot];
    if (client->has_followed_player) {
        client->followed_player.commandTime = time;
        client->followed_player.viewangles[0] = view.x;
        client->followed_player.viewangles[1] = view.y;
        client->followed_player.viewangles[2] = view.z;
        client->followed_player.viewheight = q3_source_float_to_int(height);
        client->followed_player.groundEntityNum = player->ground_entity_number;
    }
    return q3_source_movement_write(game, actor, QA_Q3_SOURCE_PM_COMMAND | QA_Q3_SOURCE_PM_VIEW, error);
}
bool qa_q3_client_movement_water(qa_q3_game *game, qa_actor_id actor, int32_t level,
                                 int32_t type, qa_error *error) {
    uint32_t slot;
    if (!game || !source_command_active(game, actor) || level < 0 || level > 3 ||
        !qa_q3_native_client_slot(game, actor, &slot, error)) return false;
    game->client_actors[slot].water_level = level;
    game->client_actors[slot].water_type = type;
    return true;
}
bool qa_q3_client_spectator_origin(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    uint32_t slot;
    qa_body_state body;
    if (!game || !source_command_active(game, actor) ||
        !qa_q3_native_client_slot(game, actor, &slot, error) ||
        !qa_world_body_read(game->options.services.world, actor, &body, error)) return false;
    q3_wire_entity_source *source = q3_wire_entity(game, actor);
    if (!source) return q3_fail(error, "Q3 SpectatorThink lost its actual source entity");
    source->authored_origin = body.origin;
    return true;
}
bool qa_q3_client_jumppad_finish(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    uint32_t slot;
    if (!game || !source_command_active(game, actor) ||
        !qa_q3_native_client_slot(game, actor, &slot, error)) return false;
    qa_q3_player_state *player = &game->client_actors[slot].state.player;
    if (player->jumppad_frame != player->pmove_frame_count)
        player->jumppad_frame = player->jumppad_entity = 0;
    qa_q3_player *copied = q3_client_follow_player(game, slot);
    if (copied) { copied->jumppadFrame = player->jumppad_frame; copied->jumppadEnt = player->jumppad_entity; }
    return q3_source_movement_write(game, actor, QA_Q3_SOURCE_PM_JUMPPAD, error);
}
bool qa_q3_client_touch_policy(const qa_q3_game *game, qa_actor_id actor,
                               bool *native, bool *touchable, bool *door, qa_error *error) {
    if (!game || !native || !touchable || !door)
        return q3_fail(error, "Q3 touch qualification needs actual source outputs");
    *native = *touchable = *door = false;
    uint32_t slot;
    if (!qa_q3_source_actor_slot(game, actor, &slot, NULL) ||
        !game->source_entities[slot].in_use) return true;
    *native = true;
    const qa_q3_map_actor_state *map = q3_map_const(game, actor);
    if (map) {
        *touchable = map->touchable;
        *door = map->kind == QA_Q3_MAP_MOVER_DOOR_TRIGGER;
        return true;
    }
    const q3_actor *entry = q3_actor_const(game, actor);
    if (!entry) return true;
    *touchable = entry->kind == Q3_ACTOR_ITEM || entry->kind == Q3_ACTOR_PROX_TRIGGER ||
        (entry->kind == Q3_ACTOR_PORTAL && entry->state.portal.source && entry->state.portal.enabled) ||
        (entry->kind == Q3_ACTOR_OBELISK && entry->state.obelisk.think == QA_Q3_OBELISK_NONE);
    return true;
}
bool qa_q3_client_think_finish(qa_q3_game *game, qa_actor_id actor, int32_t msec,
                               qa_error *error) {
    uint32_t slot;
    if (!game || !source_command_active(game, actor) || msec < 0 ||
        !qa_q3_native_client_slot(game, actor, &slot, error)) return false;
    uint32_t old;
    uint32_t buttons = (uint32_t)game->clients[slot].command.buttons;
    if (!qa_q3_client_buttons(game, actor, buttons, true, &old, error)) return false;
    qa_combat_state combat;
    if (!qa_combat_read(game->options.services.combat, actor, &combat, error)) return false;
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry) return true;
    if (combat.health <= 0) {
        qa_q3_player_state *player = &entry->state.player;
        int32_t elapsed = q3_sub_time(game->now_ms, player->respawn_after);
        uint32_t force_bits = (uint32_t)game->options.rules.force_respawn_seconds * 1000u;
        int32_t force_ms;
        memcpy(&force_ms, &force_bits, sizeof(force_ms));
        bool forced = game->options.rules.force_respawn_seconds > 0 && elapsed > force_ms;
        if (game->now_ms > player->respawn_after && (forced || (buttons & 5u)) &&
            game->options.hooks.respawn)
            return game->options.hooks.respawn(game->options.hooks.context, actor, error);
        return true;
    }
    return qa_q3_player_timers(game, actor, msec, error);
}
bool qa_q3_client_movement_water_read(const qa_q3_game *game, qa_actor_id actor,
                                      int32_t *level, int32_t *type, qa_error *error) {
    uint32_t slot;
    if (!level || !type || !qa_q3_native_client_slot(game, actor, &slot, error)) return false;
    const q3_actor *entry = q3_actor_const(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return q3_fail(error, "Q3 water observation lost its actual source entity");
    *level = entry->water_level;
    *type = entry->water_type;
    return true;
}
static bool expand_invulnerability(qa_q3_game *game, qa_actor_id actor,
                                   qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    qa_q3_player_state *player = &entry->state.player;
    uint32_t native_slot;
    if (source_command_active(game, actor) &&
        qa_q3_native_client_slot(game, actor, &native_slot, NULL)) {
        qa_q3_wire_policy policy;
        if (!qa_q3_wire_player_policy_read(game, actor, &policy, error)) return false;
        if (game->options.product != QA_Q3_TEAM_ARENA ||
            !player->powerups[QA_Q3_P_INVULNERABILITY] || (policy.pm_flags & 0x4000)) return true;
        qa_body_state body;
        if (!qa_world_body_read(game->options.services.world, actor, &body, error)) return false;
        if (!q3_actor_get(game, actor)) return true;
        qa_bounds original = body.bounds;
        body.bounds = (qa_bounds){qa_v3(-42, -42, -42), qa_v3(42, 42, 42)};
        bool okay = qa_world_body_write(game->options.services.world, actor, &body, error) &&
            qa_q3_wire_link(game, actor, NULL, error);
        bool stuck = false;
        qa_linked_body linked;
        qa_bounds source_bounds = qa_world_linked(game->options.services.world, actor, &linked)
            ? linked.absolute_bounds : (qa_bounds){0};
        for (uint32_t slot = 0; okay && !stuck && slot < QA_Q3_SOURCE_CLIENTS; ++slot) {
            qa_actor_id other = game->source_entities[slot].actor;
            if (slot == native_slot || !game->source_entities[slot].in_use ||
                !other.registry || !q3_actor_const(game, other)) continue;
            qa_combat_state combat;
            if (!qa_combat_read(game->options.services.combat, other, &combat, error)) {
                okay = false;
                break;
            }
            if (!qa_actor_id_equal(game->source_entities[slot].actor, other) || combat.health <= 0)
                continue;
            qa_bounds other_bounds = qa_world_linked(game->options.services.world, other, &linked)
                ? linked.absolute_bounds : (qa_bounds){0};
            stuck = qa_bounds_overlap(source_bounds, other_bounds);
        }
        if (okay && !stuck && q3_actor_get(game, actor)) {
            policy.pm_flags = (int32_t)((uint32_t)policy.pm_flags | 0x4000u);
            okay = qa_q3_wire_player_policy_update(game, actor, QA_Q3_WIRE_PM_FLAGS, &policy, error);
            entry = q3_actor_get(game, actor);
            if (okay && entry) entry->state.player.invulnerability_expanded = true;
        }
        if (q3_actor_get(game, actor)) {
            qa_error cleanup = {0};
            bool restored = qa_world_body_read(game->options.services.world, actor, &body, &cleanup);
            if (restored) {
                body.bounds = original;
                restored = qa_world_body_write(game->options.services.world, actor, &body, &cleanup) &&
                    qa_q3_wire_link(game, actor, NULL, &cleanup);
            }
            if (!restored) { if (okay && error) *error = cleanup; okay = false; }
        }
        return okay;
    }
    if (player->invulnerability_until <= game->now_ms || player->invulnerability_expanded)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(game->options.services.world, actor, &body, error))
        return false;
    qa_bounds bounds =
        qa_bounds_translate((qa_bounds){qa_v3(-42, -42, -42), qa_v3(42, 42, 42)}, body.origin);
    uint32_t cursor = 0;
    const qa_actor_record *record;
    while (qa_actors_next(qa_session_actors(game->options.services.session), &cursor, &record)) {
        qa_actor_id other = record->id;
        if (qa_actor_id_equal(other, actor) || !q3_is_player(game, other))
            continue;
        qa_combat_state combat;
        qa_error ignored = {0};
        if (!qa_combat_read(game->options.services.combat, other, &combat, &ignored) ||
            combat.health <= 0)
            continue;
        qa_linked_body linked;
        if (qa_world_linked(game->options.services.world, other, &linked) &&
            qa_bounds_overlap(bounds, linked.absolute_bounds))
            return true;
    }
    entry = q3_actor_get(game, actor);
    if (entry && entry->kind == Q3_ACTOR_PLAYER &&
        entry->state.player.invulnerability_until > game->now_ms)
        entry->state.player.invulnerability_expanded = true;
    return true;
}
bool qa_q3_selected_equipment_motion(qa_q3_game *game, qa_actor_id actor,
    const qa_movement_posture *crouched, qa_q3_equipment_motion *out, qa_error *error) {
    q3_actor *entry = game ? q3_actor_get(game, actor) : NULL;
    if (!entry || entry->kind != Q3_ACTOR_PLAYER ||
        !(entry->state.player.selections & QA_Q3_ARSENAL) || !crouched || !out ||
        game->source_restored || game->observation_depth == SIZE_MAX ||
        !qa_vec_finite(crouched->bounds.mins) || !qa_vec_finite(crouched->bounds.maxs) ||
        !isfinite(crouched->view_height))
        return q3_fail(error, "Selected equipment motion needs its actual arsenal player and character posture");
    qa_q3_selected_client_effects before;
    if (!qa_q3_selected_client_effects_read(game, actor, &before, error)) return false;
    ++game->observation_depth;
    bool okay = true;
    qa_q3_player_state *player = &entry->state.player;
    qa_q3_equipment_motion motion = {0};
    if (player->invulnerability_until <= game->now_ms) {
        player->invulnerability_expanded = false;
        player->selected_pm_flags &= ~0x4000u;
    } else {
        for (unsigned i = 0; i < QA_Q3_POWERUP_COUNT; ++i)
            if (player->powerups[i] < game->now_ms) player->powerups[i] = 0;
        if (game->options.product == QA_Q3_TEAM_ARENA) {
            if (player->persistent >= QA_Q3_P_SCOUT && player->persistent <= QA_Q3_P_AMMOREGEN)
                player->powerups[player->persistent] = game->now_ms;
            player->powerups[QA_Q3_P_INVULNERABILITY] = game->now_ms;
        }
        okay = expand_invulnerability(game, actor, error);
        entry = okay ? q3_actor_get(game, actor) : NULL;
        if (!entry || entry->kind != Q3_ACTOR_PLAYER ||
            !(entry->state.player.selections & QA_Q3_ARSENAL)) {
            if (okay) q3_fail(error, "Selected equipment pose lost its actual arsenal player");
            okay = false;
        }
        if (okay) {
            player = &entry->state.player;
            motion.fixed_pose = true;
            if (player->invulnerability_expanded) player->selected_pm_flags |= 0x4000u;
            motion.pose = *crouched;
            if (player->invulnerability_expanded)
                motion.pose.bounds = (qa_bounds){qa_v3(-42, -42, -42), qa_v3(42, 42, 42)};
        }
    }
    if (okay) {
        motion.speed_multiplier = game->options.product == QA_Q3_TEAM_ARENA &&
            player->persistent == QA_Q3_P_SCOUT ? 1.5 :
            player->powerups[QA_Q3_P_HASTE] != 0 ? 1.3 : 1.0;
        motion.owns_holdable_input = player->use_item_held || player->holdable != QA_Q3_H_NONE;
        okay = qa_q3_selected_client_effects_publish(game, actor, &before, error);
        if (okay) *out = motion;
    }
    --game->observation_depth;
    return okay;
}
bool qa_q3_client_think_prepare(qa_q3_game *game, qa_actor_id actor,
                                const qa_q3_usercmd *input, uint32_t *old_sequence,
                                qa_q3_usercmd *effective, qa_error *error) {
    uint32_t slot;
    if (!game || !input || !old_sequence || !effective ||
        !source_command_active(game, actor) || game->observation_depth == SIZE_MAX ||
        !qa_q3_native_client_slot(game, actor, &slot, error))
        return q3_fail(error, "Q3 ClientThink preparation needs its actual source command");
    qa_q3_usercmd command = *input;
    ++game->observation_depth;
    bool okay = true;
    qa_q3_player_state *player = &game->client_actors[slot].state.player;
    qa_actor_id hook = player->hook;
    if (player->weapon == QA_Q3_W_GRAPPLE && hook.registry && !(command.buttons & 1) &&
        q3_actor_get(game, hook))
        okay = qa_session_release(game->options.services.session, hook, error);
    q3_actor *entry = q3_actor_get(game, actor);
    if (!okay || !entry) goto done;
    player = &entry->state.player;
    *old_sequence = player->event_sequence;
    player->gauntlet_contact = false;
    if (player->weapon == QA_Q3_W_GAUNTLET && !(command.buttons & 2) &&
        (command.buttons & 1) && player->weapon_time_ms <= 0) {
        if (!game->options.hooks.primary_attack_allowed) {
            okay = q3_fail(error, "Q3 source gauntlet lacks its actual primary selection owner");
            goto done;
        }
        bool allowed = game->options.hooks.primary_attack_allowed(game->options.hooks.context, actor);
        entry = q3_actor_get(game, actor);
        if (!entry || !qa_actor_id_equal(game->source_entities[slot].actor, actor)) goto done;
        if (allowed) {
            bool hit;
            okay = q3_gauntlet(game, actor, &hit, error);
            entry = q3_actor_get(game, actor);
            if (!okay || !entry) goto done;
            entry->state.player.gauntlet_contact = hit;
        }
    }
    if (entry->force_gesture) {
        entry->force_gesture = false;
        command.buttons = (int32_t)((uint32_t)command.buttons | 8u);
        game->clients[slot].command.buttons = command.buttons;
    }
    okay = expand_invulnerability(game, actor, error);
    if (!okay || !q3_actor_get(game, actor)) goto done;
    qa_body_state body;
    okay = qa_world_body_read(game->options.services.world, actor, &body, error);
    if (okay && q3_actor_get(game, actor)) game->clients[slot].old_origin = body.origin;
done:
    *effective = command;
    --game->observation_depth;
    return okay;
}
qa_movement_control qa_q3_movement_phase_selected(void *context, qa_movement_phase phase,
                                                  qa_movement_call *call, bool arsenal_selected,
                                                  qa_error *error) {
    qa_q3_game *game = context;
    q3_actor *entry = q3_actor_get(game, call->actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return QA_MOVEMENT_CONTINUE;
    qa_q3_player_state *p = &entry->state.player;
    if (p->cutscene.active) {
        if (call->state->kind == QA_MOVEMENT_Q3)
            q3_cutscene_movement(call->state, call->command, &p->cutscene);
        return QA_MOVEMENT_CONTINUE;
    }
    if (phase == QA_MOVE_INPUT_BEGIN) {
        uint32_t native_slot;
        if (!qa_q3_native_client_slot(game, call->actor, &native_slot, NULL))
            p->last_command_ms = game->now_ms;
        if (call->command->kind == QA_MOVEMENT_Q3)
            for (unsigned i = 0; i < 3; ++i)
                p->last_command_angles[i] = call->command->angle_words[i];
        if (call->state->kind == QA_MOVEMENT_Q3) {
            p->noclip = call->state->data.q3.movement_type == 1;
            call->state->data.q3.delta_angle_words[0] = p->delta_pitch_word;
            call->state->data.q3.delta_angle_words[1] = p->delta_yaw_word;
            call->state->data.q3.delta_angle_words[2] = p->delta_roll_word;
            if (source_command_active(game, call->actor)) {
                call->state->data.q3.flags = p->flags;
                call->state->data.q3.command_time_ms = p->command_time_ms;
                call->state->data.q3.event_sequence = p->event_sequence;
                call->state->data.q3.movement_frame = p->pmove_frame_count;
                call->state->data.q3.jump_pad_frame = p->jumppad_frame;
            }
        }
    } else if (phase == QA_MOVE_INPUT_END && call->state->kind == QA_MOVEMENT_Q3) {
        qa_q3_movement_state *movement = &call->state->data.q3;
        p->command_time_ms = movement->command_time_ms;
        p->ground_entity_number = movement->ground.hit == QA_TRACE_HIT_WORLD ? 1022
                                  : movement->ground.hit == QA_TRACE_HIT_ACTOR
                                      ? q3_entity_number(game, movement->ground.actor)
                                      : 1023;
        p->delta_pitch_word = movement->delta_angle_words[0];
        p->delta_yaw_word = movement->delta_angle_words[1];
        p->delta_roll_word = movement->delta_angle_words[2];
        p->pmove_frame_count = movement->movement_frame;
        p->jumppad_frame = movement->jump_pad_frame;
        p->jumppad_entity = movement->jump_pad.registry
                                ? q3_entity_number(game, movement->jump_pad)
                                : 0;
        if (source_command_active(game, call->actor)) {
            p->flags = movement->flags;
            uint32_t slot;
            if (qa_q3_native_client_slot(game, call->actor, &slot, NULL)) {
                qa_q3_player *copied = q3_client_follow_player(game, slot);
                if (copied) copied->eFlags = (int32_t)p->flags;
            }
        }
        if (!call->prediction && !source_command_active(game, call->actor) &&
            !expand_invulnerability(game, call->actor, error))
            return QA_MOVEMENT_ERROR;
        entry = q3_actor_get(game, call->actor);
        if (!entry || entry->kind != Q3_ACTOR_PLAYER)
            return QA_MOVEMENT_REMOVED;
        p = &entry->state.player;
        if (p->invulnerability_expanded)
            movement->movement_flags |= 0x4000u;
    } else if (phase == QA_MOVE_WEAPON) {
        uint64_t teleport_revision = p->teleport_revision;
        if (call->state->kind == QA_MOVEMENT_Q3) {
            p->view_angles = call->state->data.q3.view_angles;
            p->view_height = *call->view_height;
            p->delta_yaw_word = call->state->data.q3.delta_angle_words[1];
            p->delta_pitch_word = call->state->data.q3.delta_angle_words[0];
            p->delta_roll_word = call->state->data.q3.delta_angle_words[2];
        }
        if (call->command->kind != QA_MOVEMENT_Q3)
            return QA_MOVEMENT_CONTINUE;
        if (!arsenal_selected)
            goto phase_done;
        if (!call->prediction) {
            qa_body_state body;
            if (!qa_world_body_read(game->options.services.world, call->actor, &body, error))
                return QA_MOVEMENT_ERROR;
            body.origin = qa_movement_origin(call->state);
            body.velocity = qa_movement_velocity(call->state);
            if (!qa_world_body_write(game->options.services.world, call->actor, &body, error))
                return QA_MOVEMENT_ERROR;
        }
        qa_q3_controls controls = {.attack = (call->command->buttons & 1u) != 0,
                                   .use_holdable = (call->command->buttons & 4u) != 0,
                                   .requested_weapon = (qa_q3_weapon)call->command->weapon,
                                   .prediction = call->prediction,
                                   .gauntlet_contact_known = true,
                                   .gauntlet_contact = !call->prediction && p->gauntlet_contact};
        if (!qa_q3_arsenal_step(game, call->actor, &controls, call->elapsed_seconds * 1000, error))
            return QA_MOVEMENT_ERROR;
        entry = q3_actor_get(game, call->actor);
        if (!entry)
            return QA_MOVEMENT_REMOVED;
        p = &entry->state.player;
        if (call->state->kind == QA_MOVEMENT_Q3 && source_command_active(game, call->actor))
            call->state->data.q3.event_sequence = p->event_sequence;
        if (p->teleport_revision != teleport_revision) {
            qa_body_state body;
            if (!qa_world_body_read(game->options.services.world, call->actor, &body, error) ||
                !qa_movement_set_origin(call->state, body.origin, error) ||
                !qa_movement_set_velocity(call->state, body.velocity, error))
                return QA_MOVEMENT_ERROR;
            if (call->state->kind == QA_MOVEMENT_Q3) {
                call->state->data.q3.view_angles = p->view_angles;
                call->state->data.q3.movement_time_ms = p->teleport_lock_ms;
                call->state->data.q3.movement_flags |= 0x40u;
                call->state->data.q3.delta_angle_words[0] = p->delta_pitch_word;
                call->state->data.q3.delta_angle_words[1] = p->delta_yaw_word;
                call->state->data.q3.delta_angle_words[2] = p->delta_roll_word;
            }
            call->state_replaced = true;
        }
    } else if (phase == QA_MOVE_DROP_TIMERS) {
        int32_t elapsed = (int32_t)call->milliseconds;
        p->legs_timer_ms = p->legs_timer_ms > elapsed ? p->legs_timer_ms - elapsed : 0;
        p->torso_timer_ms = p->torso_timer_ms > elapsed ? p->torso_timer_ms - elapsed : 0;
    } else if (phase == QA_MOVE_TORSO && !p->dead && !p->torso_timer_ms &&
               p->weapon_phase == QA_Q3_READY)
        p->torso_animation = (p->torso_animation & 128) | (p->weapon == QA_Q3_W_GAUNTLET ? 12 : 11);
phase_done:
    entry = q3_actor_get(game, call->actor);
    if (!entry)
        return QA_MOVEMENT_REMOVED;
    if (call->environment &&
        !qa_q3_movement_environment(game, call->actor, call->environment, error))
        return QA_MOVEMENT_ERROR;
    if (call->state->kind == QA_MOVEMENT_Q3) {
        p = &entry->state.player;
        if (p->grapple_pull) {
            call->state->data.q3.movement_flags |= 0x800u;
            call->state->data.q3.grapple_point = p->grapple_point;
        } else
            call->state->data.q3.movement_flags &= ~0x800u;
    }
    return QA_MOVEMENT_CONTINUE;
}
qa_movement_control qa_q3_movement_phase(void *context, qa_movement_phase phase,
                                         qa_movement_call *call, qa_error *error) {
    return qa_q3_movement_phase_selected(context, phase, call, true, error);
}
qa_movement_control qa_q3_movement_effect(void *context, const qa_movement_effect *effect,
                                          qa_movement_call *call, qa_error *error) {
    qa_q3_game *game = context;
    q3_actor *entry = q3_actor_get(game, call->actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return QA_MOVEMENT_CONTINUE;
    qa_q3_player_state *p = &entry->state.player;
    if (p->cutscene.active)
        return QA_MOVEMENT_CONTINUE;
    if (effect->kind == QA_MOVE_EFFECT_EVENT && call->state->kind == QA_MOVEMENT_Q3) {
        if (!q3_player_event(game, call->actor, effect->value, effect->parameter, error))
            return QA_MOVEMENT_ERROR;
        if (source_command_active(game, call->actor))
            call->state->data.q3.event_sequence = q3_actor_get(game, call->actor)
                ? q3_actor_get(game, call->actor)->state.player.event_sequence
                : call->state->data.q3.event_sequence;
        if (!call->prediction && !source_command_active(game, call->actor) &&
            (effect->value == 11 || effect->value == 12) &&
            !q3_damage(game, call->actor, (qa_actor_id){0}, (qa_actor_id){0}, QA_Q3_W_NONE, 19, 2,
                       effect->value == 12 ? 10 : 5, qa_v3(0, 0, 0), qa_v3(0, 0, 0), false, NULL,
                       error))
            return QA_MOVEMENT_ERROR;
    } else if (effect->kind == QA_MOVE_EFFECT_ANIMATION && !p->dead) {
        if (effect->animation_kind == QA_MOVE_ANIMATION_LEGS_TIMER)
            p->legs_timer_ms = effect->value;
        else if (!p->legs_timer_ms || effect->force) {
            int32_t animation = effect->value;
            if (effect->animation_kind == QA_MOVE_ANIMATION_LOCOMOTION) {
                switch ((qa_movement_locomotion)effect->value) {
                case QA_MOVE_IDLE:
                    animation = 22;
                    break;
                case QA_MOVE_WALK:
                    animation = effect->backwards ? 33 : 14;
                    break;
                case QA_MOVE_RUN:
                    animation = effect->backwards ? 16 : 15;
                    break;
                case QA_MOVE_BACKWARD:
                    animation = 16;
                    break;
                case QA_MOVE_CROUCH:
                    animation = effect->backwards ? 32 : 13;
                    break;
                case QA_MOVE_JUMP:
                    animation = effect->backwards ? 20 : 18;
                    break;
                case QA_MOVE_LAND:
                    animation = effect->backwards ? 21 : 19;
                    p->legs_timer_ms = 130;
                    break;
                case QA_MOVE_SWIM:
                    animation = 17;
                    break;
                }
            }
            if ((p->legs_animation & ~128) != animation || effect->force)
                p->legs_animation = ((p->legs_animation & 128) ^ 128) | animation;
        }
    }
    return q3_actor_get(game, call->actor) ? QA_MOVEMENT_CONTINUE : QA_MOVEMENT_REMOVED;
}
