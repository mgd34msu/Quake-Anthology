/* Port of src/content/q2/missionpacks/monsters/flyer.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/flyer.h"

#include <stdlib.h>

#include "mp_internal.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/base/monsters/flyer.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/missionpacks/monsters/rogue_common.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_flyer.h"

typedef struct flyer_bundle {
  qa_q2m_monsters *monsters;
  qa_q2mpm_monster_state *source;
} flyer_bundle;
static void run_move(qa_q2m_context *c) {
  q2mpm_set_move(c, qa_q2mpm_monster_mass(c) > 50 ? "flyer_move_kamikaze"
                                                  : c->state->stand_ground ? "flyer_move_stand" : "flyer_move_run");
}
static void run_fn(void *ctx, qa_q2m_context *c) { (void)ctx; run_move(c); }
static void return_slot(flyer_bundle *bundle, qa_q2m_context *c) {
  qa_q2f_entity *commander = c->state->has_commander ? c->game->entity(c->game->ctx, &c->state->commander) : NULL;
  qa_q2m_context *commander_context = commander ? qa_q2m_monsters_context(bundle->monsters, commander->actor.id) : NULL;
  if (commander && strcmp(commander->classname, "monster_carrier") == 0 && commander_context)
    commander_context->state->monster_slots++;
}
static void kamikaze_explode(flyer_bundle *bundle, qa_q2m_context *c) {
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  return_slot(bundle, c);
  qa_body_state enemy;
  if (entity->has_enemy && qa_q2m_enemy_body(c, &enemy)) {
    qa_vec3 origin = q2mpm_body(game, entity).origin;
    (void)q2mpm_damage(game, entity->enemy, entity, &entity->actor.id, 50, 50,
                       qa_sub3_double(enemy.origin, origin), origin, q2mpm_zero(), 0, true, 1);
  }
  qa_q2bm_explode(c, "flyer/flydeth1.wav");
}
static void kamikaze_check(flyer_bundle *bundle, qa_q2m_context *c) {
  qa_q2f_entity *entity = c->entity;
  if (!q2mpm_live(c->game, entity->actor.id)) return;
  if (!entity->has_enemy || !q2mpm_live(c->game, entity->enemy)) { kamikaze_explode(bundle, c); return; }
  entity->has_goal = true;
  entity->goal = entity->enemy;
  if (qa_q2m_target_distance(c) < 90) kamikaze_explode(bundle, c);
}
static void kamikaze_check_fn(void *ctx, qa_q2m_context *c) { kamikaze_check(ctx, c); }
static void stand_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (qa_q2mpm_monster_mass(c) > 50) run_move(c); else q2mpm_set_move(c, "flyer_move_stand");
}
static void walk_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (qa_q2mpm_monster_mass(c) > 50) run_move(c); else q2mpm_set_move(c, "flyer_move_walk");
}
static void melee_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (qa_q2mpm_monster_mass(c) > 50) run_move(c); else q2mpm_set_move(c, "flyer_move_start_melee");
}
static void attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (qa_q2mpm_monster_mass(c) > 50) { run_move(c); return; }
  double skill = c->game->options.skill;
  double chance = skill == 0 ? 0 : 1 - 0.5 / skill;
  if (q2mpm_rnd(c->game) > chance) {
    c->state->attack_state = QA_Q2M_ATTACK_STRAIGHT;
    q2mpm_set_move(c, "flyer_move_attack2");
    return;
  }
  if (q2mpm_rnd(c->game) <= 0.5) c->state->lefty = !c->state->lefty;
  c->state->attack_state = QA_Q2M_ATTACK_SLIDING;
  q2mpm_set_move(c, "flyer_move_attack3");
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx;
  if (qa_q2mpm_monster_mass(c) == 50 && qa_q2bm_flyer_definition.reactions.pain)
    qa_q2bm_flyer_definition.reactions.pain(qa_q2bm_flyer_definition.reactions.ctx, c, reaction);
}
static bool blocked_fn(void *ctx, qa_q2m_context *c, double distance) {
  flyer_bundle *bundle = ctx;
  (void)distance;
  if (qa_q2mpm_monster_mass(c) != 100)
    return qa_q2mpm_rogue_blocked_check_shot(c, 0.25 + 0.05 * c->game->options.skill, bundle->source);
  kamikaze_check(bundle, c);
  if (q2mpm_live(c->game, c->entity->actor.id)) {
    return_slot(bundle, c);
    qa_body_state body = q2mpm_body(c->game, c->entity);
    q2mpm_emit_effect(c->game, "q2:rocket-explosion",
                      qa_add3_double(body.origin, qa_scale3_double(body.velocity, -0.02)), q2mpm_zero(), 1, 0);
    c->game->remove(c->game->ctx, c->entity);
  }
  return true;
}
static void setstart_fn(void *ctx, qa_q2m_context *c) {
  flyer_bundle *bundle = ctx;
  bundle->source->flyer_next_move = QA_Q2MPM_FLYER_RUN;
  q2mpm_set_move(c, "flyer_move_start");
}
static void nextmove_fn(void *ctx, qa_q2m_context *c) {
  flyer_bundle *bundle = ctx;
  if (bundle->source->flyer_next_move == QA_Q2MPM_FLYER_RUN) q2mpm_set_move(c, "flyer_move_run");
}
static void kamikaze_init_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->entity->effects = q2mpm_or(c->entity->effects, 16);
  qa_q2f_presentation_event e;
  memset(&e, 0, sizeof e);
  e.kind = QA_Q2F_EVENT_SOUND;
  e.actor = &c->entity->actor.id;
  e.origin = q2mpm_body(c->game, c->entity).origin;
  e.path = "flyer/flyidle1.wav";
  e.channel = 0; e.volume = 1; e.attenuation = 1; e.reliable = false; e.loop = "start";
  c->game->host->emit(c->game->host->ctx, e);
}
size_t qa_q2mpm_create_rogue_flyer_definitions(qa_q2m_definition *out, qa_q2m_monsters *monsters,
                                               qa_q2mpm_monster_state *source) {
  flyer_bundle *bundle = malloc(sizeof *bundle);
  if (!bundle) qa_q2f_error("Out of memory creating rogue flyer definitions");
  bundle->monsters = monsters;
  bundle->source = source;
  qa_q2m_definition definition = qa_q2bm_flyer_definition;
  definition.bounds.min = q2mpm_v3(-16, -16, -24);
  definition.bounds.max = q2mpm_v3(16, 16, 16);
  definition.moves = qa_q2mpm_rogue_flyer_moves;
  definition.move_count = qa_q2mpm_rogue_flyer_move_count;
  definition.run = (qa_q2m_handler){NULL, run_fn};
  definition.stand = (qa_q2m_handler){NULL, stand_fn};
  definition.walk = (qa_q2m_handler){NULL, walk_fn};
  definition.melee = (qa_q2m_handler){NULL, melee_fn};
  definition.attack = (qa_q2m_handler){NULL, attack_fn};
  definition.reactions.ctx = NULL;
  definition.reactions.pain = pain_fn;
  definition.blocked_ctx = bundle;
  definition.blocked = blocked_fn;
  const qa_q2m_named_handler extra[] = {
    {"flyer_run", {NULL, run_fn}},
    {"flyer_kamikaze", {(void *)"flyer_move_kamikaze", qa_q2bm_move_handler_fn}},
    {"flyer_kamikaze_check", {bundle, kamikaze_check_fn}},
    {"flyer_setstart", {bundle, setstart_fn}},
    {"flyer_nextmove", {bundle, nextmove_fn}},
  };
  definition.callbacks = qa_q2mpm_merge_callbacks(definition.callbacks, definition.callback_count, extra, 5,
                                                  &definition.callback_count);
  out[0] = definition;
  out[1] = definition;
  out[1].classname = "monster_kamikaze";
  out[1].kind = "kamikaze";
  out[1].mass = 100;
  out[1].initialize = (qa_q2m_handler){NULL, kamikaze_init_fn};
  return 2;
}
