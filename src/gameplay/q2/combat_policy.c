#include "internal.h"
#include "items/internal.h"
#include "monsters/internal.h"
#include "qa/game_q2_combat.h"

bool qa_q2_combat_rules_read(const qa_q2_game *game, qa_q2_combat_rules *out) {
    if (!game || !out) return false;
    *out = (qa_q2_combat_rules){.owner = game->options.owner,
        .edition = game->options.edition, .product = game->options.product,
        .skill = game->options.skill,
        .deathmatch_flags = game->options.deathmatch_flags,
        .deathmatch = game->options.deathmatch, .cooperative = game->options.cooperative};
    return true;
}

bool qa_q2_combat_actor_read(const qa_q2_game *game, qa_actor_id actor,
                            qa_q2_combat_actor *out) {
    if (!game || !out || actor.slot >= game->capacity ||
        !qa_actors_get(qa_session_actors(game->services.session), actor)) return false;
    const q2_actor *state = game->actors[actor.slot];
    if (!state || !qa_actor_id_equal(state->id, actor)) return false;
    qa_q2_combat_actor view = {.character = state->client || state->monster || state->entity,
        .player = state->client != NULL, .monster = state->monster != NULL,
        .immortal = state->client && game->options.edition == QA_Q2_RERELEASE &&
            state->character_immortal,
        .no_damage_effects = game->options.edition == QA_Q2_RERELEASE &&
            state->character_no_damage_effects};
    view.mechanical = state->projectile.kind == Q2_PROX ||
        state->projectile.kind == Q2_TESLA ||
        (state->projectile.kind == Q2_TRAP && game->options.edition == QA_Q2_RERELEASE);
    if (state->monster) {
        view.mechanical |= state->monster->definition->species == Q2M_TURRET;
        view.gekk = state->monster->definition->species == Q2M_GEKK;
        view.has_enemy = state->monster->enemy.registry != 0;
        view.suppress_pain = state->monster->ducked;
        view.birth_preserves_death_knockback = game->options.edition == QA_Q2_CLASSIC;
    }
    if (state->powers && state->powers->sphere.registry &&
        state->powers->sphere.slot < game->capacity &&
        qa_actors_get(qa_session_actors(game->services.session), state->powers->sphere)) {
        const q2_actor *sphere = game->actors[state->powers->sphere.slot];
        if (sphere && qa_actor_id_equal(sphere->id, state->powers->sphere) && sphere->item &&
            sphere->item->companion &&
            qa_actor_id_equal(sphere->item->companion->owner, actor))
            view.defender_sphere = sphere->item->companion->kind == Q2_SPHERE_DEFENDER;
    }
    *out = view;
    return true;
}

bool qa_q2_combat_weapon_owned(const qa_q2_game *game, qa_item_id item) {
    if (!game || !item) return false;
    for (size_t i = 0; i < QA_Q2_WEAPON_COUNT; ++i)
        if (game->items[i] == item) return true;
    return false;
}

bool qa_q2_combat_life_read(const qa_q2_game *game, qa_actor_id actor,
                           qa_q2_combat_life *out) {
    if (!game || !out || actor.slot >= game->capacity ||
        !qa_actors_get(qa_session_actors(game->services.session), actor)) return false;
    const q2_actor *state = game->actors[actor.slot];
    if (!state || !qa_actor_id_equal(state->id, actor)) return false;
    *out = (qa_q2_combat_life){.character_owner = state->combat_life_owner,
        .birth_epoch = state->combat_life_birth_epoch, .death_ns = state->combat_death_ns,
        .source_ns = game->now_ns,
        .present = state->combat_life_present, .no_knockback = state->combat_no_knockback,
        .alive_knockback_only = state->combat_alive_knockback_only};
    return true;
}

bool qa_q2_combat_life_bind(qa_q2_game *game, qa_actor_id actor,
                           qa_actor_owner character_owner, uint64_t birth_epoch,
                           bool birth_preserves_death_knockback, qa_error *error) {
    if (!game || !character_owner || game->continuation_pending ||
        game->continuation_failed || game->restoring_continuation) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
                     "Q2 combat life requires its current source and selected CHARACTER birth");
        return false;
    }
    q2_actor *state = q2_actor_get(game, actor, true, error);
    if (!state) return false;
    if (!state->combat_life_present || state->combat_life_owner != character_owner ||
        state->combat_life_birth_epoch != birth_epoch) {
        bool retain_no_knockback = state->combat_life_present &&
            state->combat_life_owner == character_owner &&
            game->options.edition == QA_Q2_CLASSIC && birth_preserves_death_knockback &&
            state->combat_no_knockback;
        state->combat_life_owner = character_owner;
        state->combat_life_birth_epoch = birth_epoch;
        state->combat_life_present = true;
        state->combat_no_knockback = retain_no_knockback;
        state->combat_alive_knockback_only = false;
        state->combat_death_ns = 0;
    }
    return true;
}

bool qa_q2_combat_life_died(qa_q2_game *game, qa_actor_id actor,
                           qa_actor_owner character_owner, uint64_t birth_epoch,
                           bool birth_preserves_death_knockback, qa_error *error) {
    if (!qa_q2_combat_life_bind(game, actor, character_owner, birth_epoch,
                               birth_preserves_death_knockback, error)) return false;
    q2_actor *state = game->actors[actor.slot];
    if (game->options.edition == QA_Q2_RERELEASE) {
        state->combat_alive_knockback_only = true;
        state->combat_death_ns = game->now_ns;
    } else state->combat_no_knockback = true;
    return true;
}

bool qa_q2_combat_surprise(qa_q2_game *game, qa_actor_id actor, bool has_enemy,
                           bool *bonus, qa_error *error) {
    if (!game || !bonus || game->options.edition != QA_Q2_RERELEASE ||
        game->continuation_pending || game->continuation_failed || game->restoring_continuation) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 surprise requires its current COMBAT source");
        return false;
    }
    q2_actor *state = q2_actor_get(game, actor, true, error);
    if (!state) return false;
    *bonus = !has_enemy || state->combat_surprise_ns == game->now_ns;
    if (*bonus) state->combat_surprise_ns = game->now_ns;
    return true;
}

bool qa_q2_combat_power_armor_source(const qa_q2_game *game, qa_powered_armor *armor) {
    if (!game || !armor || armor->kind < QA_POWER_NONE || armor->kind > QA_POWER_SHIELD)
        return false;
    armor->source_owner = armor->kind == QA_POWER_NONE ? 0 : game->options.owner;
    armor->source_kind = QA_POWER_SOURCE_Q2;
    armor->source_edition = armor->kind == QA_POWER_NONE ? QA_Q2_POWER_ARMOR_NONE :
        game->options.edition == QA_Q2_RERELEASE ? QA_Q2_POWER_ARMOR_RERELEASE : QA_Q2_POWER_ARMOR_CLASSIC;
    return true;
}
