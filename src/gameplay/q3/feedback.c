#include "internal.h"
#include "qa/game_q3_clients.h"
#include "qa/text.h"

float q3_damage_regular_delta(const qa_damage_outcome *outcome, bool *written) {
    *written = false;
    float delta = 0;
    for (size_t i = 0; i < outcome->mutation_count; ++i) {
        const qa_damage_mutation *mutation = &outcome->mutations[i];
        if (mutation->kind != QA_MUTATION_ARMOR)
            continue;
        *written = true;
        if (mutation->value.armor.before.regular.kind != QA_ARMOR_NONE &&
            mutation->value.armor.after.regular.kind != QA_ARMOR_NONE)
            delta += (float)mutation->value.armor.before.regular.points -
                     (float)mutation->value.armor.after.regular.points;
    }
    return delta;
}
static bool feedback_event(qa_q3_game *game, qa_actor_id actor, int32_t event,
                            int32_t parameter, qa_error *error) {
    uint32_t source_slot;
    if (!qa_q3_native_client_slot(game, actor, &source_slot, NULL))
        return q3_player_event(game, actor, event, parameter, error);
    if (!q3_wire_add_event(game, actor, event, parameter, error))
        return false;
    qa_body_state body;
    if (!q3_source_body_read(game, actor, &body, error))
        return false;
    return !q3_actor_get(game, actor) ||
        q3_event(game, actor, (qa_actor_id){0}, QA_BUILTIN_ANIMATION, event, parameter,
                  body.origin, qa_v3(0, 0, 0), qa_v3(0, 0, 0), error);
}
static bool before_reaction(qa_q3_game *game, const qa_damage_outcome *outcome, qa_error *error) {
    if (!game || !outcome)
        return q3_fail(error, "invalid Q3 damage feedback");
    if (!q3_wire_damage(game, outcome, error))
        return false;
    q3_actor *entry = q3_actor_get(game, outcome->request.target);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER || outcome->stale)
        return true;
    qa_q3_player_state *player = &entry->state.player;
    player->last_hurt_client = q3_entity_number(game, outcome->request.attack.attacker);
    player->last_hurt_mod = outcome->request.attack.cause.kind == QA_CAUSE_Q3
        ? outcome->request.attack.cause.source.q3.means_of_death : 0;
    if (outcome->result.reaction == QA_REACTION_DEATH) {
        entry->enemy = outcome->request.attack.attacker;
        uint32_t source_slot;
        entry->enemy_source_present = qa_q3_source_actor_slot(game, entry->enemy, &source_slot, NULL);
        entry->enemy_source_slot = entry->enemy_source_present ? source_slot : 0;
    }
    uint32_t client_slot;
    bool source_client = qa_q3_native_client_slot(game, entry->actor, &client_slot, NULL);
    if (!(player->selections & (QA_Q3_CHARACTER | QA_Q3_EFFECTS)) && !source_client)
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
                armor += fmaxf(0, (float)mutation->value.armor.before.regular.points -
                                      (float)mutation->value.armor.after.regular.points);
        }
    player->damage_blood += blood;
    player->damage_armor += armor;
    player->damage_knockback += result->knockback;
    player->damage_from = outcome->request.direction;
    player->damage_from_world =
        qa_vec_dot(outcome->request.direction, outcome->request.direction) == 0;
    if (source_client && (game->options.rules.game_type == 4 ||
        (game->options.product == QA_Q3_TEAM_ARENA && game->options.rules.game_type == 5)) &&
        game->options.hooks.source_hurt_carrier) {
        qa_actor_id target = entry->actor;
        if (!game->options.hooks.source_hurt_carrier(game->options.hooks.context,
                target, outcome->request.attack.attacker, error)) return false;
        entry = q3_actor_get(game, target);
        uint32_t actual;
        if (!entry || !qa_q3_native_client_slot(game, target, &actual, NULL) ||
            actual != client_slot || !game->source_entities[actual].body_attached) return true;
        player = &entry->state.player;
    }
    if (result->battlesuit && !feedback_event(game, entry->actor, 62, 0, error))
        return false;
    if (source_client && result->reaction == QA_REACTION_DEATH) {
        bool admitted = false;
        if (!q3_source_initial_death(game, outcome, &admitted, error))
            return false;
        if (admitted && (!qa_q3_ranking_death(game, outcome, error) ||
                         !q3_source_death_effects(game, outcome, error)))
            return false;
    }
    return true;
}
bool qa_q3_before_reaction(qa_q3_game *game, const qa_damage_outcome *outcome, qa_error *error) {
    if (!game || !outcome || game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "invalid Q3 damage feedback boundary");
    qa_damage_outcome captured = *outcome;
    ++game->observation_depth;
    bool result = before_reaction(game, &captured, error);
    --game->observation_depth;
    return result;
}
static bool player_end_frame(qa_q3_game *game, qa_actor_id actor, int32_t water_level,
                            int32_t water_type, bool *publish, qa_error *error) {
    if (publish)
        *publish = false;
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return q3_fail(error, "missing Q3 client end frame");
    uint32_t source_slot;
    bool source_client = qa_q3_native_client_slot(game, actor, &source_slot, NULL);
    if (source_client ? game->clients[source_slot].rule.session.team == 3
                      : entry->state.player.spectator)
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
        else if (!source_client)
            player->invulnerability_expanded = false;
    }
    if (source_client ? game->match_state.intermission_time_ms != 0
                      : game->options.rules.intermission)
        return true;
    bool effects = source_client
        ? qa_q3_client_world_effects(game, actor, water_level, water_type, error)
        : qa_q3_player_effects(game, actor, 0, water_level, water_type,
                                entry->state.player.noclip, error);
    if (!effects)
        return false;
    entry = q3_actor_get(game, actor);
    uint32_t actual_slot;
    if (!entry || (!source_client && game->options.rules.intermission) || (source_client &&
        (!qa_q3_native_client_slot(game, actor, &actual_slot, NULL) || actual_slot != source_slot)))
        return true;
    player = &entry->state.player;
    bool dead = player->dead;
    if (source_client) {
        qa_q3_wire_policy policy;
        if (!qa_q3_wire_player_policy_read(game, actor, &policy, error))
            return false;
        dead = policy.pm_type == 3;
        entry = q3_actor_get(game, actor);
        if (!entry || !qa_q3_native_client_slot(game, actor, &actual_slot, NULL) ||
            actual_slot != source_slot)
            return true;
        player = &entry->state.player;
    }
    float count = fminf(255, (float)qa_source_float_to_i32(
        player->damage_blood + player->damage_armor));
    if (!dead && count != 0) {
        if (player->damage_from_world) {
            player->damage_pitch = 255;
            player->damage_yaw = 255;
            player->damage_from_world = false;
        } else {
            qa_vec3 d = player->damage_from;
            float yaw = d.x != 0 ? (((float)atan2((double)d.y, (double)d.x) * 180) / Q3_PI)
                : d.y != 0 ? (d.y > 0 ? 90 : 270) : 0;
            if (yaw < 0)
                yaw = (yaw + 360);
            float pitch = d.x == 0 && d.y == 0 ? (d.z > 0 ? 90 : 270)
                : (((float)atan2((double)d.z,
                    (double)(float)sqrt((double)((d.x * d.x) + (d.y * d.y)))) * 180) / Q3_PI);
            if (pitch < 0)
                pitch = (pitch + 360);
            player->damage_pitch = q3_source_float_to_int(((-pitch / 360) * 256));
            player->damage_yaw = q3_source_float_to_int(((yaw / 360) * 256));
        }
        qa_combat_state combat;
        if (!qa_combat_read_traits(game->options.services.combat, actor, &combat, error))
            return false;
        entry = q3_actor_get(game, actor);
        if (!entry || entry->kind != Q3_ACTOR_PLAYER)
            return true;
        player = &entry->state.player;
        if (game->now_ms > player->pain_after && !combat.invulnerable) {
            player->pain_after = q3_add_time(game->now_ms, 700);
            if (!feedback_event(game, actor, 56, qa_source_float_to_i32((float)combat.health), error))
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
    q3_wire_entity_source *source = source_client ? q3_wire_entity(game, actor) : NULL;
    if (source_client && !source)
        return q3_fail(error, "Q3 ClientEndFrame lost its published source entity");
    uint32_t flags = source ? (uint32_t)source->flags : player->flags;
    if (q3_sub_time(game->now_ms, player->last_command_ms) > 1000)
        flags |= 0x2000u;
    else
        flags &= ~0x2000u;
    if (source)
        source->flags = (int32_t)flags;
    else {
        player->flags = flags;
        if (player->reward_until && game->now_ms > player->reward_until)
            player->flags &= ~0x38848u;
    }
    int32_t loop_index = 0;
    const char *loop = game->options.product == QA_Q3_TEAM_ARENA && (flags & 2u)
                           ? "sound/weapons/proxmine/wstbtick.wav"
                       : water_level && (water_type & (8 | 16)) ? "sound/player/fry.wav"
                                                                : NULL;
    if (source_client && loop && (flags & 2u) && game->options.product == QA_Q3_TEAM_ARENA) {
        if (!qa_q3_sound_index(game, loop, &loop_index, error))
            return false;
    } else if (loop)
        loop_index = game->fry_sound_index;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    if (source_client &&
        !qa_q3_wire_player_loop_sound(game, actor, loop_index, error))
        return false;
    player = &entry->state.player;
    if (!loop)
        player->loop_sound = 0;
    else {
        qa_string_id resource;
        if (!qa_builtin_resource(&game->options.services, loop, &resource, error))
            return false;
        entry = q3_actor_get(game, actor);
        if (entry && entry->kind == Q3_ACTOR_PLAYER)
            entry->state.player.loop_sound = resource;
    }
    if (publish && qa_q3_native_client_slot(game, actor, &actual_slot, NULL) &&
        actual_slot == source_slot)
        *publish = true;
    return true;
}
bool qa_q3_player_end_frame(qa_q3_game *game, qa_actor_id actor, int32_t water_level,
                            int32_t water_type, qa_error *error) {
    if (!game || game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "invalid Q3 client end-frame boundary");
    ++game->observation_depth;
    bool result = player_end_frame(game, actor, water_level, water_type, NULL, error);
    --game->observation_depth;
    return result;
}
bool qa_q3_client_end_prepare(qa_q3_game *game, qa_actor_id actor, int32_t water_level,
                              int32_t water_type, bool *publish, qa_error *error) {
    uint32_t source_slot;
    if (!game || !publish || game->source_restored || game->observation_depth == SIZE_MAX ||
        !qa_q3_native_client_slot(game, actor, &source_slot, error))
        return q3_fail(error, "Q3 native end frame requires its actual source client");
    *publish = false;
    ++game->observation_depth;
    bool result = player_end_frame(game, actor, water_level, water_type, publish, error);
    --game->observation_depth;
    return result;
}
