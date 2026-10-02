#include "internal.h"
#include "qa/game_q1_ui.h"

bool qa_q1_player_ui_powers_read(const qa_q1_game *game, qa_actor_id actor,
    qa_q1_ui_powers *out, qa_error *error)
{
    q1_player *player = game && !game->continuation_pending ?
        q1_player_get((qa_q1_game *)game, actor) : NULL;
    if (!out || !player) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, actor.slot,
            "Q1 UI timers require their actual live player");
        return false;
    }
    qa_q1_ui_powers value = {.seconds = game->time};
    uint64_t cursor = 0;
    for (;;) {
        uint64_t order = 0;
        unsigned chosen = 0;
        for (unsigned i = 0; i < QA_Q1_POWER_COUNT; ++i)
            if (player->power_order[i] > cursor &&
                (!order || player->power_order[i] < order)) {
                order = player->power_order[i];
                chosen = i;
            }
        if (!order) break;
        cursor = order;
        if (player->power_expires[chosen] > value.seconds)
            value.powers[value.count++] = (qa_q1_ui_power){
                (qa_q1_power)chosen, player->power_expires[chosen]};
    }
    *out = value;
    return true;
}
