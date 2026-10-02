#include "internal.h"

bool qa_q2_player_animation_read(const qa_q2_game *game, qa_actor_id actor,
    qa_q2_player_animation_view *out, qa_error *error)
{
    if (!game || !out || game->continuation_pending || game->continuation_failed ||
        game->restoring_continuation) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
            "Q2 animation requires its actual completed source owner");
        return false;
    }
    q2_actor *source = q2_actor_get((qa_q2_game *)game, actor, false, error);
    if (!source || !source->client || !source->client->info.connected || source->client->corpse) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, actor.slot,
            "Q2 animation has no actual live client character");
        return false;
    }
    const q2_client_state *client = source->client;
    *out = (qa_q2_player_animation_view){.frame = client->visual.frame,
        .end_frame = client->animation_end, .priority = client->animation_priority,
        .duck = client->animation_duck, .run = client->animation_run};
    return true;
}
