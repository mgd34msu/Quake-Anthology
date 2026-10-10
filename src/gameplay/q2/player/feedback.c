#include "feedback.h"
#include "internal.h"
#include "qa/game_q2_feedback.h"

void q2_player_feedback_begin(qa_q2_game *game) {
    if (game->options.edition != QA_Q2_RERELEASE) return;
    for (q2_actor *actor = game->first_actor; actor; actor = actor->live_next)
        if (actor->client && actor->client->info.connected && q2_actor_live(game, actor->id))
            actor->client->rule.hit_marker_damage = 0;
}

bool q2_player_end_server_frames(qa_q2_game *game, qa_error *error) {
    q2_player_list *players = q2_player_list_acquire(game, false, error);
    if (!players) return false;
    size_t count = 0;
    for (size_t i = 0; i < players->count; ++i) {
        q2_player_row row = players->rows[i];
        q2_actor *actor = q2_actor_get(game, row.actor, false, NULL);
        if (!actor || !actor->client || !actor->client->info.connected) continue;
        row.info.slot = actor->client->info.slot;
        players->rows[count++] = row;
    }
    players->count = count;
    /* The source client array is visited in physical client-slot order, which
     * need not match the canonical actor registry or GAME extension order. */
    for (size_t i = 1; i < players->count; ++i) {
        q2_player_row row = players->rows[i]; size_t j = i;
        while (j && players->rows[j - 1].info.slot > row.info.slot) {
            players->rows[j] = players->rows[j - 1]; --j;
        }
        players->rows[j] = row;
    }
    bool okay = true;
    for (size_t i = 0; okay && i < players->count; ++i) {
        q2_player_row row = players->rows[i];
        q2_actor *actor = q2_actor_get(game, row.actor, false, NULL);
        if (!actor || !actor->client || !actor->client->info.connected ||
            actor->client->info.slot != row.info.slot) continue;
        okay = qa_q2_player_end_frame(game, row.actor, error);
    }
    players->active = false;
    return okay;
}

bool q2_player_frame_begin(qa_q2_game *game, qa_error *error) {
    q2_players *players = game->player_runtime;
    game->frame_stopped = players->intermission && players->next_map && players->exit;
    return qa_q2_players_frame(game, error);
}

bool qa_q2_player_hit_marker_add(qa_q2_game *game, qa_actor_id id, int64_t damage,
                                bool *found, qa_error *error) {
    if (!game || !found || game->options.edition != QA_Q2_RERELEASE ||
        game->continuation_pending || game->continuation_failed || game->restoring_continuation) {
        qa_error_set(error, QA_ERROR_ARGUMENT, id.slot,
                     "Q2 hit marker requires its current rerelease client owner");
        return false;
    }
    *found = false;
    q2_actor *actor = q2_actor_get(game, id, false, NULL);
    if (!actor || !actor->client || !actor->client->info.connected) return true;
    uint16_t word = (uint16_t)((uint64_t)actor->client->rule.hit_marker_damage + (uint64_t)damage);
    actor->client->rule.hit_marker_damage = word <= INT16_MAX ? (int32_t)word :
        (int32_t)word - INT32_C(65536);
    *found = true;
    return true;
}

bool qa_q2_player_power_armor_activate(qa_q2_game *game, qa_actor_id id,
                                      bool *found, qa_error *error) {
    if (!game || !found || game->continuation_pending || game->continuation_failed ||
        game->restoring_continuation) {
        qa_error_set(error, QA_ERROR_ARGUMENT, id.slot,
                     "Q2 power-armor pulse requires its current source client owner");
        return false;
    }
    q2_actor *actor = q2_actor_get(game, id, false, NULL);
    if (!actor || !actor->client || !actor->client->info.connected) {
        *found = false;
        return true;
    }
    actor->client->rule.power_armor_ns = q2_deadline(game->now_ns, 200 * Q2_MS);
    *found = true;
    return true;
}
