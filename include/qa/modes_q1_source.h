#ifndef QA_MODES_Q1_SOURCE_H
#define QA_MODES_Q1_SOURCE_H

#include "qa/modes.h"

/* Source numbers are independent of selected-rule team/statistics. CTF live
 * writes publish Math.fround values; its restored words retain binary64 JS
 * numbers. Rogue words belong to the actual native source entity; this owner
 * retains only that full actor reference and delegates reads and writes. */
typedef enum qa_mode_q1_number {
    QA_Q1_CTF_LAST_TEAM,
    QA_Q1_CTF_STATUS,
    QA_Q1_CTF_ACCESS,
    QA_Q1_CTF_KILLED,
    QA_Q1_CTF_MOTD,
    QA_Q1_CTF_SUICIDE_COUNT,
    QA_Q1_CTF_STUFF_COLOR,
    QA_Q1_CTF_LAST_HURT_CARRIER,
    QA_Q1_CTF_LAST_FRAGGED_CARRIER,
    QA_Q1_CTF_LAST_RETURNED,
    QA_Q1_CTF_FLAG_SINCE,
    QA_Q1_CTF_REGEN_TIME,
    QA_Q1_CTF_RUNE_NOTICE,
    QA_Q1_CTF_RESISTANCE_SOUND,
    QA_Q1_CTF_STRENGTH_SOUND,
    QA_Q1_CTF_HASTE_SOUND,
    QA_Q1_CTF_REGEN_SOUND,
    QA_Q1_CTF_OBSERVER_JUMP_HELD,
    QA_Q1_CTF_VOTED,
    QA_Q1_ROGUE_STEAM,
    QA_Q1_ROGUE_FLAGS,
    QA_Q1_ROGUE_KILLED,
    QA_Q1_ROGUE_SUICIDE_COUNT,
    QA_Q1_ROGUE_LAST_HURT_CARRIER,
    QA_Q1_ROGUE_LAST_FRAGGED_CARRIER,
    QA_Q1_ROGUE_LAST_RETURNED_FLAG,
    QA_Q1_ROGUE_FLAG_SINCE,
    QA_Q1_SOURCE_NUMBERS
} qa_mode_q1_number;

typedef struct qa_mode_q1_source_state {
    double numbers[QA_Q1_SOURCE_NUMBERS];
    qa_actor_id rogue_state;
} qa_mode_q1_source_state;

/* Admission publishes source continuation only. It does not replay selected
 * rule join, team, ranking, source birth or observer callbacks. */
bool qa_modes_q1_source_admit(qa_modes *, qa_mode_id, qa_actor_id, qa_error *);
bool qa_modes_q1_source_read(qa_modes *, qa_mode_id, qa_actor_id,
    qa_mode_q1_number, double *, qa_error *);
bool qa_modes_q1_source_write(qa_modes *, qa_mode_id, qa_actor_id,
    qa_mode_q1_number, double, qa_error *);
/* The physical source lazily creates its state and captures its actual color. */
bool qa_modes_q1_rogue_initialize(qa_modes *, qa_mode_id, qa_actor_id, qa_error *);

#endif
