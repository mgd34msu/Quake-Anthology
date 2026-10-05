#ifndef QA_GAME_Q2_ORIGINAL_SAVE_H
#define QA_GAME_Q2_ORIGINAL_SAVE_H

#include "qa/game_q2.h"
#include "qa/q2_save.h"

/* The actual GAME product selects its original representation. Resource
 * indices refer to the same engine table captured beside the module file. */
bool qa_q2_game_original_capture(qa_q2_game *, bool autosave, bool transition,
    const qa_q2_save_level *, qa_buffer *game, qa_buffer *level, qa_error *);
/* ReadGame precedes authored map spawning. ReadLevel replaces the spawned
 * physical edict state before spawn-point observation and client Begin. */
bool qa_q2_game_original_read_game(qa_q2_game *, qa_bytes, qa_error *);
bool qa_q2_game_original_read_level(qa_q2_game *, qa_bytes,
    const qa_q2_save_level *, qa_error *);
/* The normal connection and physical slot are already admitted. A live saved
 * LEVEL client restores its Source state; otherwise normal spawn follows. */
bool qa_q2_game_original_read_client(qa_q2_game *, uint32_t client_slot,
    qa_actor_id, qa_bytes game, qa_bytes level, const qa_q2_save_level *,
    bool *restored, qa_error *);

#endif
