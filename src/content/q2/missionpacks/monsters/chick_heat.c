/* Port of src/content/q2/missionpacks/monsters/chick-heat.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/chick_heat.h"

#include <stdlib.h>

#include "mp_internal.h"
#include "qa/content/q2/base/monsters/chick.h"
#include "qa/content/q2/base/monsters/common.h"

static void init_fn(void *ctx, qa_q2m_context *c) { (void)ctx; c->entity->skin = 3; }
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx;
  qa_q2bm_damaged_skin(c);
  if (q2mpm_now(c->game) < c->state->pain_time) return;
  c->state->pain_time = q2mpm_now(c->game) + 3;
  double r = q2mpm_rnd(c->game);
  q2mpm_snd(c, r < 0.33 ? "chick/chkpain1.wav" : r < 0.66 ? "chick/chkpain2.wav" : "chick/chkpain3.wav", 2);
  q2mpm_set_move(c, reaction.damage <= 10 ? "chick_move_pain1" : reaction.damage <= 25 ? "chick_move_pain2"
                                                                                        : "chick_move_pain3");
}
static void rocket_fn(void *ctx, qa_q2m_context *c) {
  qa_q2mpm_weapons *weapons = ctx;
  qa_q2bm_shot_result aim;
  if (!qa_q2bm_shot_default(c, 57, &aim)) return;
  if (c->entity->skin > 1)
    (void)weapons->fire_heat_rocket(weapons->ctx, c->entity, c->game, aim.start, aim.direction, 50, 500, 70, 50, false, 0);
  else
    (void)c->weapons.fire_rocket(c->weapons.ctx, c->entity, c->game, aim.start, aim.direction, 50, 500, 70, 50);
  qa_q2bm_muzzle(c, 57, aim.direction, aim.start);
}
qa_q2m_definition qa_q2mpm_create_chick_heat_definition(qa_q2mpm_weapons weapons) {
  qa_q2mpm_weapons *box = malloc(sizeof *box);
  if (!box) qa_q2f_error("Out of memory creating chick heat definition");
  *box = weapons;
  qa_q2m_definition definition = qa_q2bm_chick_definition;
  definition.classname = "monster_chick_heat";
  definition.initialize.ctx = NULL;
  definition.initialize.fn = init_fn;
  definition.reactions.ctx = NULL;
  definition.reactions.pain = pain_fn;
  const qa_q2m_named_handler extra[] = {{"ChickRocket", {box, rocket_fn}}};
  definition.callbacks = qa_q2mpm_merge_callbacks(definition.callbacks, definition.callback_count, extra, 1,
                                                  &definition.callback_count);
  return definition;
}
