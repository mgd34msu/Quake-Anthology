/* Port of src/content/q2/foundation/weapons/types.ts. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/weapons/types.h"

#include <string.h>


const char *qa_q2w_phase_name(qa_q2w_phase phase) {
  switch (phase) {
    case QA_Q2W_ACTIVATING: return "activating";
    case QA_Q2W_READY: return "ready";
    case QA_Q2W_FIRING: return "firing";
    case QA_Q2W_DROPPING: return "dropping";
  }
  return "activating";
}

void qa_q2w_state_init(qa_q2w_state *s, qa_q2w_name weapon) {
  memset(s, 0, sizeof *s);
  s->primary_handoff = QA_Q2W_HANDOFF_ACTIVE;
  s->weapon = weapon;
  s->phase = QA_Q2W_ACTIVATING;
  s->hand_reservation = QA_Q2W_RES_NONE;
  s->kick_duration = 0.2;
  s->loop_sound = "";
  s->gun_rate = 10;
}
void qa_q2w_state_init_default(qa_q2w_state *state) { qa_q2w_state_init(state, "blaster"); }
