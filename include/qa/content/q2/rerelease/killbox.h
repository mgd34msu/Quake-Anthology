/* Port of src/content/q2/rerelease/killbox.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_RERELEASE_KILLBOX_H
#define QA_CONTENT_Q2_RERELEASE_KILLBOX_H
#include "qa/content/q2/foundation/host.h"
#include "qa/content/q2/rerelease/players.h"
bool qa_q2_kill_rerelease_box(qa_q2_entity *e, qa_q2_game_services *g, qa_q2_rerelease_players *p, bool spawning, bool exact);
#endif
