#ifndef QA_MODES_Q3_SESSION_H
#define QA_MODES_Q3_SESSION_H

#include "qa/modes.h"

typedef enum qa_mode_q3_spectator_state {
    QA_MODE_Q3_SPECTATOR_NOT,
    QA_MODE_Q3_SPECTATOR_FREE,
    QA_MODE_Q3_SPECTATOR_FOLLOW,
    QA_MODE_Q3_SPECTATOR_SCOREBOARD
} qa_mode_q3_spectator_state;

/* Retain the selected rule-mode identity and cached settings after complete
 * canonical retirement. Native GAME clients retain their own source sess. */
bool qa_modes_q3_round_reset(qa_modes *, qa_mode_id, int32_t source_time_ms,
    uint64_t source_start_ns, int32_t restarted, qa_error *);

#endif
