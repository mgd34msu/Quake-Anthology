#include "internal.h"
#include "items/internal.h"
#include "monsters/internal.h"
#include "qa/game_q2_combat.h"

bool qa_q2_combat_rules_read(const qa_q2_game *game, qa_q2_combat_rules *out) {
    if (!game || !out) return false;
    *out = (qa_q2_combat_rules){.owner = game->options.owner,
        .edition = game->options.edition, .skill = game->options.skill,
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
        .player = state->client != NULL, .monster = state->monster != NULL};
    if (state->monster) {
        view.has_enemy = state->monster->enemy.registry != 0;
        view.suppress_pain = state->monster->ducked;
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

bool qa_q2_combat_power_armor_source(const qa_q2_game *game, qa_powered_armor *armor) {
    if (!game || !armor || armor->kind < QA_POWER_NONE || armor->kind > QA_POWER_SHIELD)
        return false;
    armor->source_owner = armor->kind == QA_POWER_NONE ? 0 : game->options.owner;
    armor->source_edition = armor->kind == QA_POWER_NONE ? QA_Q2_POWER_ARMOR_NONE :
        game->options.edition == QA_Q2_RERELEASE ? QA_Q2_POWER_ARMOR_RERELEASE : QA_Q2_POWER_ARMOR_CLASSIC;
    return true;
}
