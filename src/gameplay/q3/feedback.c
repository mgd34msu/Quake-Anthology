#include "internal.h"

bool qa_q3_before_reaction(qa_q3_game *game, const qa_damage_outcome *outcome, qa_error *error) {
    if (!game || !outcome)
        return q3_fail(error, "invalid Q3 damage feedback");
    q3_actor *entry = q3_actor_get(game, outcome->request.target);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER || outcome->stale)
        return true;
    qa_q3_player_state *player = &entry->state.player;
    if (!(player->selections & (QA_Q3_CHARACTER | QA_Q3_EFFECTS)))
        return true;
    const qa_damage_result *result = &outcome->result;
    float blood = result->applied_damage, armor = 0;
    if (result->has_feedback) {
        blood = result->blood;
        armor = result->armor_saved + result->power_saved;
    } else
        for (size_t i = 0; i < outcome->mutation_count; ++i) {
            const qa_damage_mutation *mutation = &outcome->mutations[i];
            if (mutation->kind == QA_MUTATION_ARMOR)
                armor += fmaxf(0, mutation->value.armor.before.regular.points -
                                      mutation->value.armor.after.regular.points);
        }
    player->damage_blood += blood;
    player->damage_armor += armor;
    player->damage_knockback += result->knockback;
    player->damage_from = outcome->request.direction;
    player->damage_from_world =
        qa_vec_dot(outcome->request.direction, outcome->request.direction) == 0;
    if (result->battlesuit)
        return q3_player_event(game, entry->actor, 62, 0, error);
    return true;
}
bool qa_q3_player_end_frame(qa_q3_game *game, qa_actor_id actor, int32_t water_level,
                            int32_t water_type, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return q3_fail(error, "missing Q3 client end frame");
    if (entry->state.player.spectator)
        return true;
    qa_q3_player_state *player = &entry->state.player;
    for (unsigned i = 0; i < QA_Q3_POWERUP_COUNT; ++i)
        if (player->powerups[i] < game->now_ms)
            player->powerups[i] = 0;
    if (game->options.product == QA_Q3_TEAM_ARENA) {
        if (player->persistent >= QA_Q3_P_SCOUT && player->persistent <= QA_Q3_P_AMMOREGEN)
            player->powerups[player->persistent] = game->now_ms;
        if (player->invulnerability_until > game->now_ms)
            player->powerups[QA_Q3_P_INVULNERABILITY] = game->now_ms;
        else
            player->invulnerability_expanded = false;
    }
    if (game->options.rules.intermission)
        return true;
    if (!qa_q3_player_effects(game, actor, 0, water_level, water_type, entry->state.player.noclip,
                              error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || game->options.rules.intermission)
        return true;
    player = &entry->state.player;
    float count = fminf(255, player->damage_blood + player->damage_armor);
    if (!player->dead && count > 0) {
        if (player->damage_from_world) {
            player->damage_pitch = 255;
            player->damage_yaw = 255;
            player->damage_from_world = false;
        } else {
            qa_vec3 d = player->damage_from;
            float yaw = atan2f(d.y, d.x) * 180 / Q3_PI;
            if (yaw < 0)
                yaw += 360;
            float pitch = -atan2f(d.z, sqrtf(d.x * d.x + d.y * d.y)) * 180 / Q3_PI;
            player->damage_pitch = (int32_t)(pitch / 360 * 256);
            player->damage_yaw = (int32_t)(yaw / 360 * 256);
        }
        qa_combat_state combat;
        if (!qa_combat_read(game->options.services.combat, actor, &combat, error))
            return false;
        if (game->now_ms > player->pain_after && !combat.invulnerable) {
            player->pain_after = q3_add_time(game->now_ms, 700);
            if (!q3_player_event(game, actor, 56, (int32_t)combat.health, error))
                return false;
            entry = q3_actor_get(game, actor);
            if (!entry)
                return true;
            player = &entry->state.player;
            player->damage_event = q3_add_time(player->damage_event, 1);
        }
        player->damage_count = (int32_t)count;
        player->damage_blood = player->damage_armor = player->damage_knockback = 0;
    }
    if (q3_sub_time(game->now_ms, player->last_command_ms) > 1000)
        player->flags |= 0x2000u;
    else
        player->flags &= ~0x2000u;
    if (player->reward_until && game->now_ms > player->reward_until)
        player->flags &= ~0x38848u;
    const char *loop = game->options.product == QA_Q3_TEAM_ARENA && (player->flags & 2u)
                           ? "sound/weapons/proxmine/wstbtick.wav"
                       : water_level && (water_type & (8 | 16)) ? "sound/world/fry.wav"
                                                                : NULL;
    if (!loop)
        player->loop_sound = 0;
    else if (!qa_builtin_resource(&game->options.services, loop, &player->loop_sound, error))
        return false;
    return true;
}
