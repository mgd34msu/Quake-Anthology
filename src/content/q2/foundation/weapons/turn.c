/* Port of src/content/q2/foundation/weapons/turn.ts. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/weapons/turn.h"
#include <stdint.h>
/* TS bitwise ops are int32. */
void qa_q2w_latch_buttons(qa_q2w_turn_state *s, double buttons) {
  s->latched_buttons = (double)((int32_t)s->latched_buttons | ((int32_t)buttons & ~(int32_t)s->buttons));
  s->buttons = buttons;
}
void qa_q2w_early_turn(qa_q2w_turn_state *s, qa_q2w_turn_tick tick) {
  if (((int32_t)s->latched_buttons & 1) != 0 && !s->weapon_thunk) { s->weapon_thunk = true; tick.fn(tick.ctx, true); }
}
void qa_q2w_begin_turn(qa_q2w_turn_state *s, bool allowed, qa_q2w_turn_tick tick) {
  if (allowed && !s->weapon_thunk) tick.fn(tick.ctx, ((int32_t)s->latched_buttons & 1) != 0);
  else s->weapon_thunk = false;
}
