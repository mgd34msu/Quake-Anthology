/* Port of src/content/q2/missionpacks/monsters/gladb.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/gladb.h"

#include <stdlib.h>

#include "mp_internal.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/muzzle.h"
#include "qa/content/q2/missionpacks/monsters/power_armor.h"
#include "qa/content/q2/missionpacks/monsters/tables/xatrix_gladb.h"

static void run_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2mpm_set_move(c, c->state->stand_ground ? "gladb_move_stand" : "gladb_move_run");
}
static void fire_with(qa_q2mpm_weapons *weapons, qa_q2m_context *c) {
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(c->game->options.edition, 61));
  qa_vec3 direction = qa_normalize3_double_hypot(qa_sub3_double(c->state->blind_fire_target, start));
  (void)weapons->fire_plasma(weapons->ctx, c->entity, c->game, start, direction, 100, 725, 60, 60);
}
static void fire_fn(void *ctx, qa_q2m_context *c) { fire_with(ctx, c); }
static void fire_check_fn(void *ctx, qa_q2m_context *c) {
  if (c->game->options.skill == 3) fire_with(ctx, c);
}
static void init_fn(void *ctx, qa_q2m_context *c) { (void)ctx; qa_q2mpm_monster_power_armor(c, "shield", 400); }
static void attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_vec3 eye;
  if (!qa_q2m_enemy_eye(c, &eye) || qa_q2m_target_distance(c) <= 112) return;
  q2mpm_snd(c, "weapons/plasshot.wav", 1);
  c->state->blind_fire_target = eye;
  q2mpm_set_move(c, "gladb_move_attack_gun");
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx; (void)reaction;
  qa_q2bm_damaged_skin(c);
  bool airborne = q2mpm_body(c->game, c->entity).velocity.z > 100;
  if (q2mpm_now(c->game) < c->state->pain_time) {
    if (airborne && strcmp(c->state->move->name, "gladb_move_pain") == 0) q2mpm_set_move(c, "gladb_move_pain_air");
    return;
  }
  c->state->pain_time = q2mpm_now(c->game) + 3;
  q2mpm_snd(c, q2mpm_rnd(c->game) < 0.5 ? "gladiator/pain.wav" : "gladiator/gldpain2.wav", 2);
  q2mpm_set_move(c, airborne ? "gladb_move_pain_air" : "gladb_move_pain");
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx;
  qa_q2bm_begin_death_default(c, reaction, "gladiator/glddeth2.wav", "gladb_move_death");
}
static const qa_q2bm_sound_spec sight_spec = {"gladiator/sight.wav", 2, 1};
static const qa_q2bm_sound_spec idle_spec = {"gladiator/gldidle1.wav", 2, 2};
static const qa_q2bm_sound_spec search_spec = {"gladiator/gldsrch1.wav", 2, 1};
static const qa_q2bm_sound_spec swing_spec = {"gladiator/melee1.wav", 1, 1};
static void melee_hit_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_vec3 aim = {80, q2mpm_body(c->game, c->entity).bounds.min.x, -4};
  bool hit = c->weapons.fire_hit(c->weapons.ctx, c->entity, c->game, aim,
                                 20 + floor(q2mpm_rnd(c->game) * 5), 300);
  q2mpm_snd(c, hit ? "gladiator/melee2.wav" : "gladiator/melee3.wav", 0);
}
qa_q2m_definition qa_q2mpm_create_gladb_definition(qa_q2mpm_arena *arena, qa_q2mpm_weapons weapons) {
  qa_q2mpm_weapons *box = qa_q2mpm_arena_alloc(arena, sizeof *box);
  if (!box) qa_raise("Error", 0, 0, "Out of memory creating gladb definition");
  *box = weapons;
  qa_q2m_named_handler *callbacks = qa_q2mpm_arena_alloc(arena, 6 * sizeof *callbacks);
  if (!callbacks) qa_raise("Error", 0, 0, "Out of memory creating gladb definition");
  callbacks[0] = (qa_q2m_named_handler){"gladb_run", {NULL, run_fn}};
  callbacks[1] = (qa_q2m_named_handler){"gladb_dead", {NULL, qa_q2bm_finish_corpse_handler}};
  callbacks[2] = (qa_q2m_named_handler){"gladb_cleaver_swing", {(void *)&swing_spec, qa_q2bm_sound_handler_fn}};
  callbacks[3] = (qa_q2m_named_handler){"GladbMelee", {NULL, melee_hit_fn}};
  callbacks[4] = (qa_q2m_named_handler){"gladbGun", {box, fire_fn}};
  callbacks[5] = (qa_q2m_named_handler){"gladbGun_check", {box, fire_check_fn}};
  qa_q2m_definition definition;
  memset(&definition, 0, sizeof definition);
  definition.classname = "monster_gladb";
  definition.kind = "gladb";
  definition.model = "models/monsters/gladb/tris.md2";
  definition.health = 800;
  definition.gib_health = -175;
  definition.mass = 350;
  definition.bounds.min = q2mpm_v3(-32, -32, -24);
  definition.bounds.max = q2mpm_v3(32, 32, 64);
  definition.scale = 1;
  definition.initial_move = "gladb_move_stand";
  definition.moves = qa_q2mpm_xatrix_gladb_moves;
  definition.move_count = qa_q2mpm_xatrix_gladb_move_count;
  definition.callbacks = callbacks;
  definition.callback_count = 6;
  definition.stand = qa_q2bm_move_handler("gladb_move_stand");
  definition.walk = qa_q2bm_move_handler("gladb_move_walk");
  definition.run = (qa_q2m_handler){NULL, run_fn};
  definition.attack = (qa_q2m_handler){NULL, attack_fn};
  definition.sight = qa_q2bm_sound_handler(&sight_spec);
  definition.idle = qa_q2bm_sound_handler(&idle_spec);
  definition.search = qa_q2bm_sound_handler(&search_spec);
  definition.melee = qa_q2bm_move_handler("gladb_move_attack_melee");
  definition.reactions.pain = pain_fn;
  definition.reactions.die = die_fn;
  definition.initialize = (qa_q2m_handler){NULL, init_fn};
  definition.restore = (qa_q2m_handler){NULL, qa_q2mpm_restore_monster_power_armor_handler};
  return definition;
}
