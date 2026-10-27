/* Port of src/content/q2/missionpacks/monsters/rogue-jumpers.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/rogue_jumpers.h"

#include <stdlib.h>

#include "mp_internal.h"
#include "qa/content/q2/base/monsters/berserk.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/base/monsters/parasite.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/perception.h"
#include "qa/content/q2/missionpacks/monsters/rogue_common.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_berserk.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_mutant.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_parasite.h"

typedef struct jumpers_bundle {
  qa_q2m_monsters *monsters;
  qa_q2mpm_monster_state *source;
} jumpers_bundle;
static void jump_impulse(qa_q2m_context *c, double forward_speed, double up_speed, bool timed) {
  qa_body_state body = q2mpm_body(c->game, c->entity);
  qa_angle_vectors axes = qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE);
  if (timed) c->entity->timestamp = q2mpm_now(c->game);
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_velocity = true;
  change.velocity = qa_add3_double(body.velocity, qa_add3_double(qa_scale3_double(axes.forward, forward_speed),
                                                                  qa_scale3_double(axes.up, up_speed)));
  q2mpm_move_body(c->game, c->entity, change, false, false);
}
static void jump_wait(qa_q2m_context *c, bool timed) {
  c->state->next_frame = c->entity->frame
      + (q2mpm_body(c->game, c->entity).ground != NULL || (timed && qa_q2mpm_monster_jump_finished(c)) ? 1 : 0);
}
static void jump_to(qa_q2m_context *c, const char *up, const char *down, bool dodge) {
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return;
  if (dodge) qa_q2m_finish_dodge(c);
  q2mpm_set_move(c, enemy.origin.z > q2mpm_body(c->game, c->entity).origin.z ? up : down);
}
static void berserk_run_move(qa_q2m_context *c) {
  qa_q2m_finish_dodge(c);
  if (qa_q2bm_berserk_definition.run.fn) qa_q2bm_berserk_definition.run.fn(qa_q2bm_berserk_definition.run.ctx, c);
}
static void berserk_run_fn(void *ctx, qa_q2m_context *c) { (void)ctx; berserk_run_move(c); }
static void berserk_melee_move(qa_q2m_context *c) {
  qa_q2m_finish_dodge(c);
  q2mpm_set_move(c, ((uint32_t)floor(q2mpm_rnd(c->game) * 0x8000) & 1u) == 0 ? "berserk_move_attack_spike"
                                                                              : "berserk_move_attack_club");
}
static void berserk_melee_fn(void *ctx, qa_q2m_context *c) { (void)ctx; berserk_melee_move(c); }
static void berserk_sidestep_move(qa_q2m_context *c) {
  const char *name = c->state->move->name;
  if (strcmp(name, "berserk_move_jump") == 0 || strcmp(name, "berserk_move_jump2") == 0
      || strcmp(name, "berserk_move_run1") == 0) return;
  q2mpm_set_move(c, "berserk_move_run1");
}
static void berserk_sidestep_fn(void *ctx, qa_q2m_context *c) { (void)ctx; berserk_sidestep_move(c); }
static void berserk_dodge_fn(void *ctx, qa_q2m_context *c, qa_actor_id attacker, double eta,
                             const qa_trace_result *trace, bool gravity) {
  jumpers_bundle *bundle = ctx;
  (void)gravity;
  qa_q2mpm_duck_fn duck = {NULL, NULL};
  qa_q2m_handler sidestep = {NULL, berserk_sidestep_fn};
  qa_q2mpm_rogue_monster_dodge(c, bundle->monsters, attacker, eta, trace, duck, sidestep);
}
static bool berserk_blocked_fn(void *ctx, qa_q2m_context *c, double distance) {
  (void)ctx;
  if (qa_q2mpm_blocked_check_jump_default(c, distance, 256, 40) != QA_Q2MPM_JUMP_NONE) {
    jump_to(c, "berserk_move_jump2", "berserk_move_jump", true);
    return true;
  }
  return qa_q2mpm_blocked_check_platform(c, distance);
}
static void berserk_pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx;
  qa_q2bm_damaged_skin(c);
  if (q2mpm_now(c->game) < c->state->pain_time) return;
  c->state->pain_time = q2mpm_now(c->game) + 3;
  q2mpm_snd(c, "berserk/berpain2.wav", 2);
  if (c->game->options.skill == 3) return;
  qa_q2m_finish_dodge(c);
  q2mpm_set_move(c, reaction.damage < 20 || q2mpm_rnd(c->game) < 0.5 ? "berserk_move_pain1" : "berserk_move_pain2");
}
static void done_dodge_fn(void *ctx, qa_q2m_context *c) { (void)ctx; qa_q2m_finish_dodge(c); }
static bool mutant_blocked_fn(void *ctx, qa_q2m_context *c, double distance) {
  (void)ctx;
  if (qa_q2mpm_blocked_check_jump_default(c, distance, 256, 68) != QA_Q2MPM_JUMP_NONE) {
    jump_to(c, "mutant_move_jump_up", "mutant_move_jump_down", false);
    return true;
  }
  return qa_q2mpm_blocked_check_platform(c, distance);
}
static bool parasite_blocked_fn(void *ctx, qa_q2m_context *c, double distance) {
  jumpers_bundle *bundle = ctx;
  if (qa_q2mpm_rogue_blocked_check_shot(c, 0.25 + 0.05 * c->game->options.skill, bundle->source)) return true;
  if (qa_q2mpm_blocked_check_jump_default(c, distance, 256, 68) != QA_Q2MPM_JUMP_NONE) {
    jump_to(c, "parasite_move_jump_up", "parasite_move_jump_down", false);
    return true;
  }
  return qa_q2mpm_blocked_check_platform(c, distance);
}
static bool parasite_check_fn(void *ctx, qa_q2m_context *c) {
  jumpers_bundle *bundle = ctx;
  if (!qa_q2m_default_check_attack(c, NULL)) return false;
  qa_trace_result trace;
  if (!qa_q2mpm_rogue_parasite_drain_trace(c, &trace)) return false;
  if (trace.hit.kind != QA_TRACE_HIT_ACTOR || !c->entity->has_enemy
      || !qa_same_actor(trace.hit.actor, c->entity->enemy)) {
    qa_q2mpm_state_get(bundle->source, c->entity)->blocked = true;
    c->attack(c->impl_ctx, c);
    qa_q2mpm_state_get(bundle->source, c->entity)->blocked = false;
  }
  return true;
}
#define IMPULSE_FN(name, fwd, up, timed) \
  static void name(void *ctx, qa_q2m_context *c) { (void)ctx; jump_impulse(c, fwd, up, timed); }
IMPULSE_FN(berserk_jump_fn, 100, 300, true)
IMPULSE_FN(berserk_jump2_fn, 150, 400, true)
IMPULSE_FN(mutant_up_fn, 200, 450, false)
IMPULSE_FN(mutant_down_fn, 100, 300, false)
IMPULSE_FN(parasite_up_fn, 200, 450, true)
IMPULSE_FN(parasite_down_fn, 100, 300, true)
static void wait_timed_fn(void *ctx, qa_q2m_context *c) { (void)ctx; jump_wait(c, true); }
static void wait_plain_fn(void *ctx, qa_q2m_context *c) { (void)ctx; jump_wait(c, false); }

size_t qa_q2mpm_create_rogue_jumping_monsters(qa_q2m_definition *out, qa_q2m_monsters *monsters,
                                              qa_q2mpm_monster_state *source) {
  jumpers_bundle *bundle = malloc(sizeof *bundle);
  if (!bundle) qa_q2f_error("Out of memory creating rogue jumping monsters");
  bundle->monsters = monsters;
  bundle->source = source;
  qa_q2m_definition berserk = qa_q2bm_berserk_definition;
  berserk.moves = qa_q2mpm_rogue_berserk_moves;
  berserk.move_count = qa_q2mpm_rogue_berserk_move_count;
  berserk.run = (qa_q2m_handler){NULL, berserk_run_fn};
  berserk.melee = (qa_q2m_handler){NULL, berserk_melee_fn};
  berserk.attack = (qa_q2m_handler){NULL, berserk_melee_fn};
  berserk.reactions.ctx = NULL;
  berserk.reactions.pain = berserk_pain_fn;
  berserk.dodge_ctx = bundle;
  berserk.dodge = berserk_dodge_fn;
  berserk.blocked_ctx = bundle;
  berserk.blocked = berserk_blocked_fn;
  {
    const qa_q2m_named_handler extra[] = {
      {"berserk_run", {NULL, berserk_run_fn}},
      {"monster_done_dodge", {NULL, done_dodge_fn}},
      {"berserk_jump_now", {NULL, berserk_jump_fn}},
      {"berserk_jump2_now", {NULL, berserk_jump2_fn}},
      {"berserk_jump_wait_land", {NULL, wait_timed_fn}},
    };
    berserk.callbacks = qa_q2mpm_merge_callbacks(berserk.callbacks, berserk.callback_count, extra, 5,
                                                 &berserk.callback_count);
  }
  out[0] = berserk;
  qa_q2m_definition mutant = qa_q2mpm_base_create_mutant_definition(monsters, "rogue");
  mutant.moves = qa_q2mpm_rogue_mutant_moves;
  mutant.move_count = qa_q2mpm_rogue_mutant_move_count;
  mutant.blocked_ctx = bundle;
  mutant.blocked = mutant_blocked_fn;
  {
    const qa_q2m_named_handler extra[] = {
      {"mutant_jump_up", {NULL, mutant_up_fn}},
      {"mutant_jump_down", {NULL, mutant_down_fn}},
      {"mutant_jump_wait_land", {NULL, wait_plain_fn}},
    };
    mutant.callbacks = qa_q2mpm_merge_callbacks(mutant.callbacks, mutant.callback_count, extra, 3,
                                                &mutant.callback_count);
  }
  out[1] = mutant;
  qa_q2m_definition parasite = qa_q2bm_parasite_definition;
  parasite.moves = qa_q2mpm_rogue_parasite_moves;
  parasite.move_count = qa_q2mpm_rogue_parasite_move_count;
  parasite.blocked_ctx = bundle;
  parasite.blocked = parasite_blocked_fn;
  parasite.check_attack_ctx = bundle;
  parasite.check_attack = parasite_check_fn;
  {
    const qa_q2m_named_handler extra[] = {
      {"parasite_jump_up", {NULL, parasite_up_fn}},
      {"parasite_jump_down", {NULL, parasite_down_fn}},
      {"parasite_jump_wait_land", {NULL, wait_timed_fn}},
    };
    parasite.callbacks = qa_q2mpm_merge_callbacks(parasite.callbacks, parasite.callback_count, extra, 3,
                                                  &parasite.callback_count);
  }
  out[2] = parasite;
  return 3;
}
