/* Port of src/content/q2/foundation/weapons/damage.ts. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/weapons/damage.h"
double qa_q2w_weapon_damage_multiplier(qa_actor_id actor, qa_q2w_damage_input input, double now,
                                       const qa_q2w_hooks *hooks) {
  bool quad = input.quad_until > now;
  return (quad ? (hooks->quad_multiplier ? hooks->quad_multiplier(hooks->ctx, actor) : 4) : 1)
       * (input.double_until > now && !(quad && input.no_stack_double) ? 2 : 1)
       * (hooks->source_damage_multiplier ? hooks->source_damage_multiplier(hooks->ctx, actor) : 1);
}
