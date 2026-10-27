/* Port of src/content/q2/foundation/weapons/turn.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_FOUNDATION_WEAPONS_TURN_H
#define QA_CONTENT_Q2_FOUNDATION_WEAPONS_TURN_H
#include <stdbool.h>
typedef struct qa_q2w_turn_state { double buttons, latched_buttons; bool weapon_thunk; } qa_q2w_turn_state;
typedef struct qa_q2w_turn_tick { void *ctx; void (*fn)(void *ctx, bool latched_attack); } qa_q2w_turn_tick;
void qa_q2w_latch_buttons(qa_q2w_turn_state *state, double buttons);
void qa_q2w_early_turn(qa_q2w_turn_state *state, qa_q2w_turn_tick tick);
void qa_q2w_begin_turn(qa_q2w_turn_state *state, bool allowed, qa_q2w_turn_tick tick);
#endif
