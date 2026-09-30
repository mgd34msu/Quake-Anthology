#ifndef QA_GAME_Q3_ROUND_H
#define QA_GAME_Q3_ROUND_H

#include "qa/game_q3.h"

typedef struct qa_q3_round_source {
    qa_q3_product product;
    int32_t game_type, start_time_ms, current_time_ms;
    uint32_t random_seed, max_clients;
} qa_q3_round_source;

/* The installed, fully spawned native map's actual source values. This does
 * not decide whether the application's composition or clients permit reset. */
bool qa_q3_round_read(const qa_q3_game *, qa_q3_round_source *, qa_error *);

/* After genuine canonical retirement, retain the installed authored source
 * callbacks, targets and original seed. The caller supplies current source
 * clock and effective source warmup/restarted settings, then runs ordinary
 * authored spawn/post-spawn and ordered native client admission. */
bool qa_q3_round_reset(qa_q3_game *, int32_t source_time_ms, bool warmup,
                       int32_t restarted, qa_error *);

#endif
