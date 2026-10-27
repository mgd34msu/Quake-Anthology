/* Port of src/content/q2/rerelease/monsters/guardian.ts.
 * Rerelease m_guardian.cpp. ZeniMax Media, GPL-2.0. */
#include "qa/content/q2/rerelease/monsters/guardian.h"

#include <stdlib.h>

#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/gibs.h"
#include "qa/content/q2/foundation/monsters/muzzle.h"
#include "qa/content/q2/rerelease/monsters/beam.h"
#include "qa/content/q2/rerelease/monsters/boss.h"
#include "qa/content/q2/rerelease/monsters/common.h"
#include "qa/content/q2/rerelease/monsters/tables/flashes.h"
#include "qa/content/q2/rerelease/monsters/tables/guardian.h"
#include "rrm_internal.h"

static void run_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2rm_set_move(c, c->state->stand_ground ? "guardian_move_stand" : "guardian_move_run");
}
static void spin_sound(qa_q2m_context *c, bool playing) {
  q2rm_emit_loop(c->game, c->entity, "weapons/hyprbl1a.wav", 1, playing ? "start" : "stop");
}
static void fire_update_fn(void *ctx, qa_q2f_entity *beam, qa_q2f_game *game) {
  qa_q2m_monsters *monsters = ctx;
  qa_q2m_context *context = beam->has_owner ? qa_q2m_monsters_context(monsters, beam->owner) : NULL;
  qa_q2f_entity *owner = game->entity(game->ctx, beam->has_owner ? &beam->owner : NULL);
  qa_body_state enemy_body;
  if (!context || !owner || !qa_q2m_enemy_body(context, &enemy_body)) return;
  qa_q2f_entity *enemy = game->entity(game->ctx, owner->has_enemy ? &owner->enemy : NULL);
  if (!owner->has_enemy) return;
  qa_body_state_result enemy_actor = game->host->bodies.read(game->host->bodies.ctx, owner->enemy);
  if (!enemy_actor.has_value) return;
  qa_vec3 offset = q2rm_has(owner->frame, 1) ? q2rm_v3(125, -70, 60) : q2rm_v3(112, -62, 60);
  qa_vec3 start = qa_q2m_project_flash_default(context, offset);
  qa_vec3 target;
  if (enemy) {
    target = qa_q2rm_random_body_point(enemy, game);
  } else {
    qa_body_state b = enemy_actor.value;
    target.x = b.origin.x + b.bounds.min.x + q2rm_rnd(game) * (b.bounds.max.x - b.bounds.min.x);
    target.y = b.origin.y + b.bounds.min.y + q2rm_rnd(game) * (b.bounds.max.y - b.bounds.min.y);
    target.z = b.origin.z + b.bounds.min.z + q2rm_rnd(game) * (b.bounds.max.z - b.bounds.min.z);
  }
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_origin = true; change.origin = start;
  q2rm_move_body(game, beam, change, false, false);
  beam->movedir = qa_normalize3_double_hypot(qa_sub3_double(target, start));
  qa_q2rm_update_monster_beam(beam, game, false);
}
static void attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return;
  double range = qa_q2m_target_distance(c);
  q2rm_set_move(c, range > 500 ? "guardian_move_atk2_in"
      : c->state->melee_time < q2rm_now(c->game) && range < 120 ? "guardian_move_kick" : "guardian_move_atk1_in");
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx;
  qa_q2f_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  qa_q2f_game *game = c->game;
  if ((!qa_q2rm_chainfist(c) && reaction.damage <= 10) || q2rm_now(game) < state->pain_time) return;
  if (!qa_q2rm_chainfist(c) && reaction.damage <= 75 && q2rm_rnd(game) > 0.2) return;
  const qa_q2rm_guardian_frames *f = &qa_q2rm_guardian_frames_value;
  double frame = entity->frame;
  if ((frame >= f->atk1_spin1 && frame <= f->atk1_spin15) || (frame >= f->atk2_fire1 && frame <= f->atk2_fire4)
      || (frame >= f->kick_in1 && frame <= f->kick_in13)) return;
  state->pain_time = q2rm_now(game) + 3;
  if (!qa_q2rm_reacts_to_pain(c)) return;
  spin_sound(c, false);
  q2rm_set_move(c, "guardian_move_pain1");
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx; (void)reaction;
  spin_sound(c, false);
  c->state->dead = true;
  c->state->can_take_damage = true;
  q2rm_set_damageable(c->game, c->entity, true);
  q2rm_set_move(c, "guardian_move_death");
}
static void atk1_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->entity->timestamp = q2rm_now(c->game) + 0.65 + q2rm_rnd(c->game) * 1.5;
  q2rm_set_move(c, "guardian_move_atk1_spin");
}
static void atk1_charge_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  spin_sound(c, true);
  q2rm_snd(c, "weapons/hyprbu1a.wav", 1);
}
static void atk1_finish_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  spin_sound(c, false);
  q2rm_set_move(c, "guardian_atk1_out");
}
static void fire_blaster_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_vec3 eye;
  if (!qa_q2m_enemy_eye(c, &eye)) return;
  qa_q2f_game *game = c->game;
  qa_q2f_entity *entity = c->entity;
  double flash = qa_q2rm_rereleaseFlash_value.GUARDIAN_BLASTER;
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(QA_Q2F_RERELEASE, flash));
  qa_vec3 target = {eye.x + (q2rm_rnd(game) * 2 - 1) * 5, eye.y + (q2rm_rnd(game) * 2 - 1) * 5,
                    eye.z + (q2rm_rnd(game) * 2 - 1) * 5};
  qa_vec3 direction = qa_normalize3_double_hypot(qa_sub3_double(target, start));
  double effects = fmod(entity->frame, 4) == 0 ? 64 : 0;
  (void)c->weapons.fire_blaster(c->weapons.ctx, entity, game, start, direction, 2, 1000, effects, false, false, 0);
  qa_q2rm_monster_flash(c, flash, start, direction);
  if (qa_q2m_health(game, entity->has_enemy ? &entity->enemy : NULL) > 0
      && entity->frame == qa_q2rm_guardian_frames_value.atk1_spin12 && entity->timestamp > q2rm_now(game)
      && qa_q2m_visible_enemy(c))
    c->state->next_frame = qa_q2rm_guardian_frames_value.atk1_spin5;
}
static void laser_fire_fn(void *ctx, qa_q2m_context *c) {
  qa_q2f_think update = {ctx, fire_update_fn};
  q2rm_snd(c, "weapons/laser2.wav", 1);
  qa_q2rm_fire_monster_beam(c, 25, q2rm_has(c->entity->frame, 1), update);
}
static void kick_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (!c->weapons.fire_hit(c->weapons.ctx, c->entity, c->game, q2rm_v3(80, 0, -80), 85, 700))
    c->state->melee_time = q2rm_now(c->game) + 1;
}
static void dead_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2f_game *game = c->game;
  qa_q2f_entity *entity = c->entity;
  for (int i = 0; i < 3; i++)
    q2rm_emit_effect(game, "q2:explosion1-big", qa_q2rm_random_body_point(entity, game), q2rm_zero(), 1, 0);
  qa_q2m_gib_options plain = {false, false, false, false};
  qa_q2m_gib_options metal = {false, true, false, false};
  qa_q2m_gib_options metal_head = {true, true, false, false};
  for (int i = 0; i < 2; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_meat/tris.md2", 125, plain);
  for (int i = 0; i < 4; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_metal/tris.md2", 125, metal);
  static const char *const gibs[6] = {
    "models/monsters/guardian/gib1.md2", "models/monsters/guardian/gib2.md2", "models/monsters/guardian/gib3.md2",
    "models/monsters/guardian/gib4.md2", "models/monsters/guardian/gib5.md2", "models/monsters/guardian/gib6.md2"};
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 2; j++) (void)qa_q2m_throw_gib(entity, game, gibs[i], 125, metal);
  (void)qa_q2m_throw_gib(entity, game, "models/monsters/guardian/gib7.md2", 125, metal_head);
  c->state->gibbed = true;
}
static const qa_q2bm_sound_spec footstep_spec = {"zortemp/step.wav", 4, 1};

qa_q2m_definition qa_q2rm_create_guardian_definition(qa_q2m_monsters *monsters) {
  qa_q2m_named_handler *callbacks = malloc(11 * sizeof *callbacks);
  qa_q2f_named_think *think_defs = malloc(3 * sizeof *think_defs);
  if (!callbacks || !think_defs) qa_q2f_error("Out of memory creating guardian definition");
  callbacks[0] = (qa_q2m_named_handler){"guardian_run", {NULL, run_fn}};
  callbacks[1] = (qa_q2m_named_handler){"guardian_footstep", {(void *)&footstep_spec, qa_q2bm_sound_handler_fn}};
  callbacks[2] = (qa_q2m_named_handler){"BossExplode", {NULL, qa_q2rm_boss_explode_handler}};
  callbacks[3] = (qa_q2m_named_handler){"guardian_atk1", {NULL, atk1_fn}};
  callbacks[4] = (qa_q2m_named_handler){"guardian_atk1_charge", {NULL, atk1_charge_fn}};
  callbacks[5] = (qa_q2m_named_handler){"guardian_atk1_finish", {NULL, atk1_finish_fn}};
  callbacks[6] = (qa_q2m_named_handler){"guardian_atk2", {(void *)"guardian_move_atk2_fire", qa_q2bm_move_handler_fn}};
  callbacks[7] = (qa_q2m_named_handler){"guardian_atk2_out", {(void *)"guardian_move_atk2_out", qa_q2bm_move_handler_fn}};
  callbacks[8] = (qa_q2m_named_handler){"guardian_fire_blaster", {NULL, fire_blaster_fn}};
  callbacks[9] = (qa_q2m_named_handler){"guardian_laser_fire", {monsters, laser_fire_fn}};
  callbacks[10] = (qa_q2m_named_handler){"guardian_kick", {NULL, kick_fn}};
  const qa_q2m_named_handler extra[1] = {{"guardian_dead", {NULL, dead_fn}}};
  size_t callback_count = 0;
  qa_q2m_named_handler *merged = qa_q2rm_merge_callbacks(callbacks, 11, extra, 1, &callback_count);
  free(callbacks);
  think_defs[0].name = "guardian_fire_update";
  think_defs[0].cb.ctx = monsters; think_defs[0].cb.fn = fire_update_fn;
  think_defs[1].name = "beam_think";
  think_defs[1].cb = qa_q2rm_free_monster_beam();
  think_defs[2].name = "BossExplode_think";
  think_defs[2].cb = qa_q2rm_boss_explode_think();
  qa_q2m_definition definition;
  memset(&definition, 0, sizeof definition);
  definition.classname = "monster_guardian";
  definition.kind = "guardian";
  definition.model = "models/monsters/guardian/tris.md2";
  definition.health = 2500; definition.gib_health = -200; definition.mass = 850; definition.scale = 1;
  definition.bounds.min = q2rm_v3(-96, -96, -66);
  definition.bounds.max = q2rm_v3(96, 96, 62);
  definition.initial_move = "guardian_move_stand";
  definition.moves = qa_q2rm_guardian_moves;
  definition.move_count = qa_q2rm_guardian_move_count;
  definition.callbacks = merged;
  definition.callback_count = callback_count;
  definition.stand = qa_q2bm_move_handler("guardian_move_stand");
  definition.walk = qa_q2bm_move_handler("guardian_move_walk");
  definition.run = (qa_q2m_handler){NULL, run_fn};
  definition.attack = (qa_q2m_handler){NULL, attack_fn};
  definition.reactions.pain = pain_fn;
  definition.reactions.die = die_fn;
  definition.has_source_callbacks = true;
  definition.source_callbacks.think = think_defs;
  definition.source_callbacks.think_count = 3;
  return definition;
}
