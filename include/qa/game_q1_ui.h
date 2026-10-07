#ifndef QA_GAME_Q1_UI_H
#define QA_GAME_Q1_UI_H

#include "qa/game_q1.h"
#include "qa/game_q1_wire.h"

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

/* Native player fields and inventory at its own clock; no physical source
 * slot or loaded wire world is required. Wire and local HUD share this read. */
bool qa_q1_player_ui_read(const qa_q1_game *, qa_actor_id, qa_q1_wire_player *, qa_error *);

/* Copies the actual timed-power Map in insertion order at its source clock.
 * No expiration, callback, admission or timer mutation runs. */
bool qa_q1_player_ui_powers_read(const qa_q1_game *, qa_actor_id,
    qa_q1_ui_powers *, qa_error *);

#endif
