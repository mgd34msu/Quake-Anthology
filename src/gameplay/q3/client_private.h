#ifndef QA_Q3_CLIENT_PRIVATE_H
#define QA_Q3_CLIENT_PRIVATE_H

#include "internal.h"
#include "qa/game_q3_clients.h"

static inline bool q3_client_slot(const qa_q3_game *game, qa_actor_id actor,
                                  uint32_t *out) {
    return game && out && qa_q3_native_client_slot(game, actor, out, NULL);
}

static inline q3_client_state *q3_client_actor(qa_q3_game *game,
    qa_actor_id actor, qa_error *error) {
    uint32_t slot;
    if (!game || game->source_restored || !q3_client_slot(game, actor, &slot)) {
        q3_fail(error, "Q3 client has no admitted native source slot");
        return NULL;
    }
    return q3_client_at(game, slot);
}

#endif
