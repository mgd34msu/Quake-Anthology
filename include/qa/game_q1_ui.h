#ifndef QA_GAME_Q1_UI_H
#define QA_GAME_Q1_UI_H

#include "qa/game_q1.h"

/* Original Rogue Sbar_DrawFace only replaces the face in CTF modes. */
static inline bool qa_q1_rogue_team_face_active(uint32_t max_clients, double teamplay)
{ return max_clients != 1 && teamplay > 3 && teamplay < 7; }

typedef struct qa_q1_ui_power {
    qa_q1_power power;
    double expires;
} qa_q1_ui_power;
typedef struct qa_q1_ui_powers {
    qa_q1_ui_power powers[QA_Q1_POWER_COUNT];
    size_t count;
    double seconds;
} qa_q1_ui_powers;

/* Copies the actual timed-power Map in insertion order at its source clock.
 * No expiration, callback, admission or timer mutation runs. */
bool qa_q1_player_ui_powers_read(const qa_q1_game *, qa_actor_id,
    qa_q1_ui_powers *, qa_error *);

#endif
