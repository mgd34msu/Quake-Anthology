/* Port of src/content/q2/foundation/weapons/damage.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_FOUNDATION_WEAPONS_DAMAGE_H
#define QA_CONTENT_Q2_FOUNDATION_WEAPONS_DAMAGE_H
#include "qa/content/q2/foundation/weapons/types.h"
/* input: Pick<Q2WeaponInput,"quadUntil"|"doubleUntil"|"noStackDouble">;
 * hooks: Pick<hooks,"quadMultiplier"|"sourceDamageMultiplier">. */
typedef struct qa_q2w_damage_input { double quad_until, double_until; bool no_stack_double; } qa_q2w_damage_input;
double qa_q2w_weapon_damage_multiplier(qa_actor_id actor, qa_q2w_damage_input input, double now,
                                       const qa_q2w_hooks *hooks);
#endif
