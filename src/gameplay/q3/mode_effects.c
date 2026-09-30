#include "internal.h"

bool qa_q3_player_request_weapon(qa_q3_game *game, qa_actor_id actor,
                                  qa_q3_weapon weapon, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER ||
        !(entry->state.player.selections & QA_Q3_ARSENAL) ||
        weapon <= QA_Q3_W_NONE || weapon >= QA_Q3_WEAPON_COUNT ||
        (game->options.product == QA_Q3_ARENA && weapon > QA_Q3_W_GRAPPLE) ||
        game->observation_depth == SIZE_MAX)
        return q3_fail(error, "invalid Q3 weapon request");
    ++game->observation_depth;
    bool owned = q3_owns_weapon(game, actor, weapon);
    entry = q3_actor_get(game, actor);
    if (owned && entry && entry->kind == Q3_ACTOR_PLAYER)
        entry->state.player.requested_weapon = weapon;
    --game->observation_depth;
    return true;
}

static bool quad(qa_q3_game *game, qa_actor_id actor,
                   uint64_t duration_ns, bool stack, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    uint64_t milliseconds = duration_ns / UINT64_C(1000000);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER ||
        !(entry->state.player.selections & (QA_Q3_EFFECTS | QA_Q3_CHARACTER)) ||
        milliseconds > INT32_MAX)
        return q3_fail(error, "invalid Q3 timed quad grant");
    int32_t base = game->now_ms;
    if (stack && entry->state.player.powerups[QA_Q3_P_QUAD] > base)
        base = entry->state.player.powerups[QA_Q3_P_QUAD];
    entry->state.player.powerups[QA_Q3_P_QUAD] = stack || duration_ns
        ? q3_add_time(base, (int32_t)milliseconds) : 0;
    return true;
}
bool qa_q3_player_quad(qa_q3_game *game, qa_actor_id actor,
                         uint64_t duration_ns, qa_error *error) {
    return quad(game, actor, duration_ns, false, error);
}
bool qa_q3_player_quad_stack(qa_q3_game *game, qa_actor_id actor,
                               uint64_t duration_ns, qa_error *error) {
    return quad(game, actor, duration_ns, true, error);
}
