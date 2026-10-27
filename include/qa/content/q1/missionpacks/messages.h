/* Port of src/content/q1/missionpacks/messages.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_MESSAGES_H
#define QA_CONTENT_Q1_MISSIONPACKS_MESSAGES_H
#include "qa/content/q1/missionpacks/types.h"
void qa_q1mp_mission_message(qa_q1mp_game *game, qa_q1mp_opt_actor player, const char *key);
void qa_q1mp_mission_pickup_message(qa_q1mp_game *game, qa_actor_id player, const char *key);
/* Classic string for key, or key itself (test/debug surface). */
const char *qa_q1mp_mission_classic(const char *key);
#endif
