#include "internal.h"

bool qa_q3_player_notarget(qa_q3_game *game, qa_actor_id actor, bool *enabled, qa_error *error) {
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
    return prediction || !q3_actor_get(game, actor) ||
           q3_use_holdable(game, actor, expected, error);
}
bool qa_q3_activate_holdable(qa_q3_game *game, qa_actor_id actor, qa_q3_holdable expected,
                             bool prediction, qa_error *error) {
    if (!game || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "invalid Q3 holdable action boundary");
    ++game->observation_depth;
    bool okay = activate_holdable(game, actor, expected, prediction, error);
    --game->observation_depth;
    return okay;
}

bool qa_q3_bind_player_begin(qa_q3_game *game, qa_actor_id actor, uint32_t selections,
                             int32_t handicap, qa_q3_player_binding *binding, qa_error *error) {
    if (!game || actor.slot >= game->capacity ||
        !qa_actors_get(qa_session_actors(game->options.services.session), actor) || !selections ||
        (selections & ~(uint32_t)QA_Q3_ALL_SELECTIONS) || !binding || binding->token ||
        game->player_binding_tokens[actor.slot])
        return q3_fail(error, "invalid Q3 player admission");
    q3_actor *entry = &game->actors[actor.slot];
    if (entry->kind && (!qa_actor_id_equal(entry->actor, actor) || entry->kind != Q3_ACTOR_PLAYER))
        return q3_fail(error, "actor already has another Q3 behavior");
    if (handicap < 1 || handicap > 100)
        handicap = 100;
    if (++game->player_binding_serial == 0)
        ++game->player_binding_serial;
    game->player_binding_tokens[actor.slot] = game->player_binding_serial;
    *binding = (qa_q3_player_binding){.actor = actor,
                                      .token = game->player_binding_serial,
                                      .prior_selections = entry->kind
                                                              ? entry->state.player.selections
                                                              : 0,
                                      .selections = selections,
                                      .handicap = handicap,
                                      .created = !entry->kind};
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
    q3_actor *entry = &game->actors[binding->actor.slot];
    if ((binding->created && entry->kind) ||
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
        *entry = (q3_actor){.actor = binding->actor,
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
    if (active == binding->token)
        game->player_binding_tokens[binding->actor.slot] = 0;
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
bool qa_q3_grapple_read(const qa_q3_game *game, qa_actor_id actor, qa_q3_grapple_state *out) {
    const q3_actor *entry = q3_actor_const(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER || !out)
        return false;
    const qa_q3_player_state *player = &entry->state.player;
    *out = (qa_q3_grapple_state){.hook = player->hook,
                                 .point = player->grapple_point,
                                 .active = player->grapple_pull &&
                                           q3_actor_const(game, player->hook) != NULL,
                                 .fire_held = player->fire_held};
    return true;
}
bool qa_q3_release_grapple(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    qa_actor_id hook = entry->state.player.hook;
    entry->state.player.fire_held = entry->state.player.grapple_pull = false;
    entry->state.player.hook = (qa_actor_id){0};
    return !q3_actor_get(game, hook) ||
           qa_session_release(game->options.services.session, hook, error);
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
    return (int32_t)((uint32_t)(int32_t)(fmodf(angle, 360) * 65536 / 360) & 65535u);
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
        q3_angle_word(angles.x) - player->last_command_angles[0];
    player->delta_yaw_word =
        q3_angle_word(angles.y) - player->last_command_angles[1];
    player->delta_roll_word =
        q3_angle_word(angles.z) - player->last_command_angles[2];
    ++player->teleport_revision;
    player->teleport_lock_ms = lock_ms;
}
bool q3_player_state_valid(const qa_q3_player_state *state) {
    if (!state || state->weapon < 0 || state->weapon >= QA_Q3_WEAPON_COUNT ||
        state->requested_weapon < 0 || state->requested_weapon >= QA_Q3_WEAPON_COUNT ||
        state->weapon_phase < QA_Q3_READY || state->weapon_phase > QA_Q3_FIRING ||
        state->external_slot < QA_Q3_SLOT_ACTIVE ||
        state->external_slot > QA_Q3_SLOT_RESUME_REQUESTED ||
        (state->persistent != QA_Q3_P_NONE &&
         (state->persistent < QA_Q3_P_SCOUT || state->persistent > QA_Q3_P_AMMOREGEN)) ||
        state->holdable < QA_Q3_H_NONE || state->holdable > QA_Q3_H_INVULNERABILITY ||
        state->max_health < 1 || state->handicap < 1 || state->handicap > 100 ||
        state->drowning_damage < 0 || state->drowning_damage > 15 ||
        !isfinite(state->fractional_weapon_ms) || state->fractional_weapon_ms < 0 ||
        state->fractional_weapon_ms >= 1 || !qa_vec_finite(state->view_angles) ||
        !qa_vec_finite(state->grapple_point) || !isfinite(state->view_height) ||
        !qa_vec_finite(state->cutscene.origin) || !qa_vec_finite(state->cutscene.angles) ||
        !qa_vec_finite(state->cutscene.view_offset) ||
        (state->cutscene.active && !(state->selections & QA_Q3_CHARACTER)) ||
        !qa_vec_finite(state->damage_from) || !isfinite(state->damage_blood) ||
        !isfinite(state->damage_armor) || !isfinite(state->damage_knockback) ||
        !state->selections || (state->selections & ~(uint32_t)QA_Q3_ALL_SELECTIONS))
        return false;
    for (size_t i = 0; i < 3; ++i)
        if (state->last_command_angles[i] < 0 || state->last_command_angles[i] > 65535)
            return false;
    return true;
}
bool qa_q3_player_restore(qa_q3_game *game, qa_actor_id actor, const qa_q3_player_state *state,
                          qa_error *error) {
    if (!q3_player_state_valid(state))
        return q3_fail(error, "invalid Q3 player checkpoint");
    if (!qa_q3_bind_player(game, actor, state->selections, state->handicap, error))
        return false;
    game->actors[actor.slot].state.player = *state;
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
bool qa_q3_spawn_player(qa_q3_game *game, qa_actor_id actor, const qa_body_state *spawn,
                        qa_team_id team, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER || !spawn)
        return q3_fail(error, "missing Q3 spawn admission");
    qa_q3_player_state *player = &entry->state.player;
    if (player->dead && (player->selections & QA_Q3_CHARACTER) &&
        !q3_copy_corpse(game, actor, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry)
        return true;
    player = &entry->state.player;
    if (player->selections & QA_Q3_CHARACTER) {
        qa_combat_state combat = {
            .health = (float)player->handicap + 25,
            .mass = 200,
            .can_take_damage = true,
            .team = team,
            .armor.regular = {.kind = QA_ARMOR_Q3, .protection.q3_protection = 0.66f}};
        qa_combat_state previous;
        qa_error ignored = {0};
        if (qa_combat_read(game->options.services.combat, actor, &previous, &ignored)) {
            if (!qa_combat_set_health(game->options.services.combat, actor, combat.health, error) ||
                !qa_combat_set_armor(game->options.services.combat, actor, &combat.armor, error) ||
                !qa_combat_set_traits(game->options.services.combat, actor, &combat, error))
                return false;
        } else if (!qa_combat_create_actor(game->options.services.combat, actor, &combat, error))
            return false;
        qa_body_state body = *spawn;
        body.bounds = (qa_bounds){qa_v3(-15, -15, -24), qa_v3(15, 15, 32)};
        qa_body_state previous_body;
        if (qa_world_body_read(game->options.services.world, actor, &previous_body, &ignored)) {
            if (!qa_world_body_write(game->options.services.world, actor, &body, error))
                return false;
        } else if (!qa_world_body_create(game->options.services.world, actor, &body, error))
            return false;
        qa_actor_collision collision = {.family = QA_COLLISION_Q3,
                                        .shape = QA_SHAPE_BOX,
                                        .contents = Q3_CONTENTS_BODY,
                                        .role = QA_COLLISION_SOLID};
        if (!qa_world_set_collision(game->options.services.world, actor, &collision, error))
            return false;
    }
    if (player->selections & QA_Q3_ARSENAL) {
        qa_inventory_entry inventory[26];
        size_t count = 0;
        int limit = game->options.product == QA_Q3_ARENA ? 11 : QA_Q3_WEAPON_COUNT;
        for (int weapon = 1; weapon < limit; ++weapon) {
            inventory[count++] = (qa_inventory_entry){
                .item = game->weapon_items[weapon],
                .capacity = 1,
                .count = weapon == QA_Q3_W_GAUNTLET || weapon == QA_Q3_W_MACHINEGUN ? 1 : 0,
                .policy = QA_COUNT_SOURCE_INT32};
            if (game->ammo_items[weapon])
                inventory[count++] = (qa_inventory_entry){
                    .item = game->ammo_items[weapon],
                    .capacity = 200,
                    .count = weapon == QA_Q3_W_MACHINEGUN
                                 ? (game->options.rules.game_type == 3 ? 50 : 100)
                                 : 0,
                    .policy = QA_COUNT_SOURCE_INT32};
        }
        if (!qa_inventory_has(game->options.services.inventory, actor)) {
            if (!qa_inventory_create_actor(game->options.services.inventory, actor, inventory,
                                           count, error))
                return false;
        } else
            for (size_t i = 0; i < count; ++i)
                if (!qa_inventory_configure(game->options.services.inventory, actor, &inventory[i],
                                            NULL, NULL, error))
                    return false;
        player->weapon = player->requested_weapon = QA_Q3_W_MACHINEGUN;
        memcpy(player->ammo_regeneration_items, game->ammo_items,
               sizeof(player->ammo_regeneration_items));
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
        if (game->options.services.actor_traits(game->options.services.context, actor, &traits)) {
            if (isfinite(traits.max_health) && traits.max_health >= 1 &&
                traits.max_health < 2147483648.0f)
                player->max_health = (int32_t)traits.max_health;
            if (isfinite(traits.view_height))
                player->view_height = traits.view_height;
        }
    }
    player->invulnerability_until = 0;
    player->invulnerability_expanded = false;
    player->last_command_ms = game->now_ms;
    player->damage_blood = player->damage_armor = player->damage_knockback = 0;
    player->loop_sound = 0;
    player->damage_count = player->damage_event = player->damage_pitch = player->damage_yaw = 0;
    player->time_residual = player->pain_after = player->reward_until = player->last_kill_ms =
        player->rail_streak = 0;
    player->fractional_weapon_ms = 0;
    player->gauntlet_contact = player->damage_from_world = player->noclip = false;
    memset(player->ammo_time_ms, 0, sizeof(player->ammo_time_ms));
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
    player->grapple_pull = player->fire_held = player->use_item_held = false;
    ++player->spawn_count;
    if (!qa_q3_inventory_admit(game, actor, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    player = &entry->state.player;
    if (!(player->selections & QA_Q3_CHARACTER))
        return true;
    q3_force_view(player, spawn->angles, 100);
    if (!player->spectator && !q3_killbox(game, actor, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry)
        return true;
    player = &entry->state.player;
    if (player->spectator) {
        if (!qa_world_unlink(game->options.services.world, actor, error))
            return false;
    } else if (!qa_world_link(game->options.services.world, actor, NULL, error))
        return false;
    if (game->options.services.motion_changed) {
        qa_body_state body;
        if (!qa_world_body_read(game->options.services.world, actor, &body, error))
            return false;
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
    if (!entry)
        return true;
    player = &entry->state.player;
    return player->spawn_count <= 1 || q3_player_event(game, actor, 42, 0, error);
}
static void torso(qa_q3_player_state *player, int32_t animation) {
    if (!player->dead)
        player->torso_animation = ((player->torso_animation & 128) ^ 128) | animation;
}
bool qa_q3_map_ammo_regeneration(qa_q3_game *game, qa_actor_id actor, qa_q3_weapon weapon,
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
    }
    if (entry->state.player.ammo_regeneration_items[weapon] != ammo)
        entry->state.player.ammo_time_ms[weapon] = 0;
    entry->state.player.ammo_regeneration_items[weapon] = ammo;
    return true;
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
static bool drop_weapon(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    if (!q3_player_event(game, actor, 22, 0, error))
        return false;
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry)
        return true;
    entry->state.player.weapon_phase = QA_Q3_DROPPING;
    entry->state.player.weapon_time_ms = q3_add_time(entry->state.player.weapon_time_ms, 200);
    torso(&entry->state.player, 9);
    return true;
}
static bool raise_weapon(qa_q3_game *game, qa_actor_id actor, qa_q3_player_state *player) {
    qa_q3_weapon requested = player->requested_weapon;
    bool owned = q3_owns_weapon(game, actor, requested);
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return false;
    player = &entry->state.player;
    player->weapon = owned ? requested : QA_Q3_W_NONE;
    player->weapon_phase = QA_Q3_RAISING;
    player->weapon_time_ms = q3_add_time(player->weapon_time_ms, 250);
    torso(player, 10);
    return true;
}
static bool arsenal_step(qa_q3_game *game, qa_actor_id actor, const qa_q3_controls *command,
                         float elapsed_ms, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER || !command || !isfinite(elapsed_ms) ||
        elapsed_ms < 0 || elapsed_ms > INT_MAX - 1024.0f)
        return q3_fail(error, "invalid Q3 arsenal command");
    qa_q3_player_state *player = &entry->state.player;
    if (player->cutscene.active)
        return true;
    bool attack = command->attack, use = command->use_holdable;
    qa_combat_state combat;
    if (!qa_combat_read(game->options.services.combat, actor, &combat, error))
        return false;
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
        if (!command->prediction && (player->selections & QA_Q3_CHARACTER) &&
            game->now_ms > player->respawn_after && (attack || use || forced) &&
            game->options.hooks.respawn)
            return game->options.hooks.respawn(game->options.hooks.context, actor, error);
        return true;
    }
    if (player->respawned)
        return true;
    if (use && !player->use_item_held) {
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
            if (!raise_weapon(game, actor, player))
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
        if (!drop_weapon(game, actor, error))
            return false;
        entry = q3_actor_get(game, actor);
        if (entry)
            entry->state.player.external_slot = QA_Q3_SLOT_DROPPING;
        return true;
    }
    if ((player->weapon_time_ms <= 0 || player->weapon_phase != QA_Q3_FIRING) && switch_weapon &&
        player->weapon_phase != QA_Q3_DROPPING)
        if (!drop_weapon(game, actor, error))
            return false;
    entry = q3_actor_get(game, actor);
    if (!entry)
        return true;
    player = &entry->state.player;
    if (player->weapon_time_ms > 0)
        return true;
    if (player->weapon_phase == QA_Q3_DROPPING) {
        (void)raise_weapon(game, actor, player);
        return true;
    }
    if (player->weapon_phase == QA_Q3_RAISING) {
        player->weapon_phase = QA_Q3_READY;
        torso(player, player->weapon == QA_Q3_W_GAUNTLET ? 12 : 11);
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
    torso(player, player->weapon == QA_Q3_W_GAUNTLET ? 8 : 7);
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
    if (!q3_player_event(game, actor, 23, 0, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    if (!command->prediction && !qa_q3_fire_weapon(game, actor, weapon, error))
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
    else if (player->persistent == QA_Q3_P_AMMOREGEN || player->powerups[QA_Q3_P_HASTE])
        add = (int32_t)((float)add / 1.3f);
    player->weapon_time_ms = q3_add_time(player->weapon_time_ms, add);
    return true;
}
bool qa_q3_arsenal_step(qa_q3_game *game, qa_actor_id actor, const qa_q3_controls *command,
                        float elapsed_ms, qa_error *error) {
    if (!game || game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "invalid Q3 arsenal action boundary");
    ++game->observation_depth;
    bool okay = arsenal_step(game, actor, command, elapsed_ms, error);
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
    entry->state.player.flags |= 0x80u;
    if (!q3_cancel_kamikaze_timers(game, actor, error))
        return false;
    if (!q3_actor_get(game, actor))
        return true;
    qa_combat_state state;
    if (!qa_combat_read_traits(game->options.services.combat, actor, &state, error))
        return false;
    state.can_take_damage = false;
    return qa_combat_set_traits(game->options.services.combat, actor, &state, error) &&
           qa_world_unlink(game->options.services.world, actor, error) &&
           q3_player_event(game, actor, 64, killer, error);
}
bool qa_q3_damage_reaction(qa_q3_game *game, const qa_damage_outcome *outcome, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, outcome->request.target);
    if (!entry || outcome->stale)
        return true;
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
        if (!qa_world_body_read(game->options.services.world, corpse, &body, error))
            return false;
        entry->state.corpse.flags |= 0x80u;
        combat.can_take_damage = false;
        if (!q3_cancel_kamikaze_timers(game, corpse, error))
            return false;
        if (!q3_actor_get(game, corpse))
            return true;
        return qa_combat_set_traits(game->options.services.combat, corpse, &combat, error) &&
               qa_world_unlink(game->options.services.world, corpse, error) &&
               q3_event(game, corpse, outcome->request.attack.attacker, QA_BUILTIN_DEATH, 64, 0,
                        body.origin, qa_v3(0, 0, 0), qa_v3(0, 0, 0), error);
    }
    if (entry->kind == Q3_ACTOR_MISSILE && outcome->result.reaction == QA_REACTION_DEATH) {
        entry->state.missile.phase = Q3_MISSILE_PROX_TRIGGERED;
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
    if (player->dead) {
        if (combat.health <= -40 && game->options.rules.blood)
            return gib(game, actor, killer, error);
        return combat.health > -40 ||
               qa_combat_set_health(game->options.services.combat, actor, -39, error);
    }
    player->dead = true;
    player->flags |= 1u;
    player->respawn_after = q3_add_time(game->now_ms, 1700);
    player->deaths = q3_add_time(player->deaths, 1);
    qa_actor_id hook = player->hook, mine = player->attached_mine;
    if (q3_actor_get(game, hook) &&
        !qa_session_release(game->options.services.session, hook, error))
        return false;
    if (q3_actor_get(game, mine) &&
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
    if (!q3_death_rewards(game, actor, &outcome->request, error))
        return false;
    if (!q3_actor_get(game, actor))
        return true;
    if (game->options.hooks.death &&
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
    if (!qa_q3_player_death_cleanup(game, actor, error))
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
    player->weapon = QA_Q3_W_NONE;
    memset(player->powerups, 0, sizeof(player->powerups));
    player->loop_sound = 0;
    if (!qa_world_set_collision(game->options.services.world, actor, &corpse_collision, error) ||
        !qa_world_body_write(game->options.services.world, actor, &body, error) ||
        !qa_world_link(game->options.services.world, actor, NULL, error))
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
    if (!q3_player_event(game, actor, event, killer, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry)
        return true;
    player = &entry->state.player;
    if (game->options.product == QA_Q3_TEAM_ARENA && (player->flags & 0x200u))
        return q3_schedule_kamikaze(game, actor, body.origin, error);
    return true;
}
bool qa_q3_player_timers(qa_q3_game *game, qa_actor_id actor, int32_t elapsed, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER || elapsed < 0)
        return q3_fail(error, "invalid Q3 player effects");
    qa_q3_player_state *player = &entry->state.player;
    if (!(player->selections & (QA_Q3_EFFECTS | QA_Q3_CHARACTER)))
        return true;
    qa_combat_state combat;
    if (!qa_combat_read(game->options.services.combat, actor, &combat, error))
        return false;
    player->time_residual = q3_add_time(player->time_residual, elapsed);
    while (player->time_residual >= 1000 && combat.health > 0) {
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
        } else if ((player->selections & QA_Q3_CHARACTER) && health > (float)maximum)
            health -= 1;
        if (!qa_combat_set_health(game->options.services.combat, actor, health, error))
            return false;
        if (regenerated && !q3_player_event(game, actor, 63, 0, error))
            return false;
        entry = q3_actor_get(game, actor);
        if (!entry)
            return true;
        player = &entry->state.player;
        if (!qa_combat_read(game->options.services.combat, actor, &combat, error))
            return false;
        if ((player->selections & QA_Q3_CHARACTER) &&
            combat.armor.regular.points > (float)maximum &&
            !qa_combat_set_regular_points(game->options.services.combat, actor,
                                          combat.armor.regular.points - 1, NULL, error))
            return false;
        entry = q3_actor_get(game, actor);
        if (!entry)
            return true;
        player = &entry->state.player;
        if (!qa_combat_read(game->options.services.combat, actor, &combat, error))
            return false;
    }
    if (player->persistent == QA_Q3_P_AMMOREGEN) {
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
            player->ammo_time_ms[weapon] = total;
        }
    }
    return true;
}
bool qa_q3_player_effects(qa_q3_game *game, qa_actor_id actor, int32_t elapsed, int32_t water_level,
                          int32_t water_type, bool noclip, qa_error *error) {
    if (water_level < 0 || water_level > 3)
        return q3_fail(error, "invalid Q3 world effects");
    if (!qa_q3_player_timers(game, actor, elapsed, error))
        return false;
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry)
        return true;
    qa_q3_player_state *player = &entry->state.player;
    player->noclip = noclip;
    if (!(player->selections & QA_Q3_EFFECTS) || player->spectator ||
        game->options.rules.intermission)
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
            return q3_player_event(game, actor, 62, 0, error);
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
                             qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER || !input ||
        !qa_actor_id_equal(input->actor, actor))
        return q3_fail(error, "invalid Q3 movement input");
    qa_q3_player_state *player = &entry->state.player;
    player->gauntlet_contact = false;
    if (!player->cutscene.active && !input->prediction && input->command.kind == QA_MOVEMENT_Q3 &&
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
    if (!game || game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "invalid Q3 movement preparation boundary");
    ++game->observation_depth;
    bool okay = prepare_movement(game, actor, input, error);
    --game->observation_depth;
    return okay;
}
static bool expand_invulnerability(qa_q3_game *game, qa_actor_id actor,
                                   qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    qa_q3_player_state *player = &entry->state.player;
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
        p->last_command_ms = game->now_ms;
        if (call->command->kind == QA_MOVEMENT_Q3)
            for (unsigned i = 0; i < 3; ++i)
                p->last_command_angles[i] = call->command->angle_words[i] & 65535;
        if (call->state->kind == QA_MOVEMENT_Q3) {
            p->noclip = call->state->data.q3.movement_type == 1;
            call->state->data.q3.delta_angle_words[0] = p->delta_pitch_word;
            call->state->data.q3.delta_angle_words[1] = p->delta_yaw_word;
            call->state->data.q3.delta_angle_words[2] = p->delta_roll_word;
        }
    } else if (phase == QA_MOVE_INPUT_END && call->state->kind == QA_MOVEMENT_Q3) {
        qa_q3_movement_state *movement = &call->state->data.q3;
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
        if (!call->prediction && !expand_invulnerability(game, call->actor, error))
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
        if (!call->prediction && (effect->value == 11 || effect->value == 12) &&
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
