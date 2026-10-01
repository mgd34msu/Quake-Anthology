#include "internal.h"

void qa_q1_game_finale_reset(qa_q1_game *game)
{
    if (!game || game->destroy_pending) return;
    game->finale_polled = game->finale_acknowledged = false;
    game->finale_last_poll = 0;
    for (uint32_t slot = 0; slot < game->capacity; ++slot) {
        q1_player *player = game->players[slot];
        if (player) player->finale_held_present = player->finale_held = false;
    }
}

bool qa_q1_game_finale_finished(qa_q1_game *game)
{
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(game, &operation, NULL)) return false;
    bool reset = !game->finale_polled || game->time < game->finale_last_poll ||
        game->time - game->finale_last_poll > 1;
    if (reset) game->finale_acknowledged = false;
    game->finale_polled = true;
    game->finale_last_poll = game->time;
    for (uint32_t slot = 0; slot < game->capacity; ++slot) {
        q1_player *player = game->players[slot];
        if (!player) continue;
        if (!player->source_client || !qa_q1_player_source_present(game, player->id)) {
            player->finale_held_present = player->finale_held = false;
            continue;
        }
        bool attack = player->input.attack;
        qa_actor_id actor = player->id;
        if (game->host.client_attack &&
            !game->host.client_attack(game->host.context, player->id, &attack)) attack = false;
        if (!qa_q1_game_operation_live(&operation)) break;
        player = game->players[slot];
        if (!player || !player->source_client || !qa_actor_id_equal(player->id, actor) ||
            !qa_q1_player_source_present(game, actor)) continue;
        if (reset) {
            player->finale_held_present = true;
            player->finale_held = attack;
        }
        if (attack && (!player->finale_held_present || !player->finale_held))
            game->finale_acknowledged = true;
        player->finale_held_present = true;
        player->finale_held = attack;
    }
    bool finished = qa_q1_game_operation_live(&operation) && game->finale_acknowledged;
    qa_q1_game_operation_end(&operation);
    return finished;
}
