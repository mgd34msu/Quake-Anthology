/* Port of src/content/q2/missionpacks/monsters/soldierh.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/soldierh.h"
#include "qa/foundation/js.h"

#include <stdio.h>
#include <stdlib.h>

#include "mp_internal.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/gibs.h"
#include "qa/content/q2/foundation/monsters/muzzle.h"
#include "qa/content/q2/missionpacks/monsters/dabeam.h"
#include "qa/content/q2/missionpacks/monsters/tables/xatrix_soldierh.h"
#include "qa/content/q2/missionpacks/types.h"

static const double blaster_flashes[] = {39, 40, 83, 86, 89, 92, 95, 98};
static const double machinegun_flashes[] = {43, 44, 85, 88, 91, 94, 97, 100};
static void stand_move(qa_q2m_context *c) {
  q2mpm_set_move(c, strcmp(c->state->move->name, "soldierh_move_stand3") == 0 || q2mpm_rnd(c->game) < 0.8
                        ? "soldierh_move_stand1" : "soldierh_move_stand3");
}
static void stand_fn(void *ctx, qa_q2m_context *c) { (void)ctx; stand_move(c); }
static void run_move(qa_q2m_context *c) {
  if (c->state->stand_ground) { q2mpm_set_move(c, "soldierh_move_stand1"); return; }
  const char *name = c->state->move->name;
  q2mpm_set_move(c, strcmp(name, "soldierh_move_walk1") == 0 || strcmp(name, "soldierh_move_walk2") == 0
                    || strcmp(name, "soldierh_move_start_run") == 0 ? "soldierh_move_run" : "soldierh_move_start_run");
}
static void run_fn(void *ctx, qa_q2m_context *c) { (void)ctx; run_move(c); }
static bool refire_check(qa_q2m_context *c) {
  return (c->game->options.skill == 3 && q2mpm_rnd(c->game) < 0.5) || qa_q2m_target_distance(c) < 80;
}
static void duck_down(qa_q2m_context *c) {
  if (c->state->ducked) return;
  qa_q2m_set_duck(c, true);
  c->state->pause_time = q2mpm_now(c->game) + 1;
}
static void pain_sound(qa_q2m_context *c, bool death) {
  int32_t n = qa_js_to_int32(c->entity->skin) | 1;
  int variant = n == 1 ? 2 : n == 3 ? 1 : 3;
  char buffer[40];
  snprintf(buffer, sizeof buffer, "soldier/%s%d.wav", death ? "soldeth" : "solpain", variant);
  q2mpm_snd(c, buffer, 2);
}
static void fire_at(qa_q2mpm_weapons *weapons, qa_q2m_context *c, double index) {
  double left;
  double right2;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  double flash = q2mpm_at_d(entity->skin < 4 ? blaster_flashes : machinegun_flashes, 8, index);
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(game->options.edition, flash));
  qa_vec3 direction = qa_math_angle_vectors_rule(q2mpm_body(game, entity).angles, QA_ANGLE_RULE_NATIVE).forward;
  if (index != 5 && index != 6) {
    qa_vec3 a;
    qa_vec3 b;
    qa_vec3 eye;
    if (!qa_q2m_enemy_eye(c, &eye)) return;
    qa_angle_vectors aim = qa_math_angle_vectors_rule(
        qa_vector_to_angles_with_rule(qa_sub3_double(eye, start), QA_VECTOR_TO_ANGLES_NATIVE_AI), QA_ANGLE_RULE_NATIVE);
    direction = qa_normalize3_double_hypot(qa_add3_double(qa_scale3_double(aim.forward, 8192),
        (a = qa_scale3_double(aim.right, (q2mpm_rnd(game) * 2 - 1) * 100), b = qa_scale3_double(aim.up, (q2mpm_rnd(game) * 2 - 1) * 50), qa_add3_double(a,
                       b))));
  }
  if (entity->skin < 2) {
    (void)weapons->fire_ion_ripper(weapons->ctx, entity, game, start, direction, 5, 600, 0x100000);
    qa_q2bm_muzzle(c, flash, direction, start);
    return;
  }
  if (entity->skin < 4) {
    (void)weapons->fire_blue_blaster(weapons->ctx, entity, game, start, direction, 1, 600, 0x400000);
    qa_q2bm_muzzle(c, 17, direction, start);
    return;
  }
  if (!state->hold_frame) state->pause_time = (left = q2mpm_now(game), right2 = (3 + floor(q2mpm_rnd(game) * 8)) * 0.1, left + right2);
  if (q2mpm_rnd(game) > 0.8) q2mpm_snd4(c, "misc/lasfly.wav", 0, 1, 3);
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return;
  qa_body_state body = q2mpm_body(game, entity);
  qa_vec3 aim_angles = qa_vector_to_angles_with_rule(qa_sub3_double(enemy.origin, body.origin), QA_VECTOR_TO_ANGLES_NATIVE_AI);
  qa_angle_vectors aim = qa_math_angle_vectors_rule(aim_angles, QA_ANGLE_RULE_NATIVE);
  qa_vec3 offset = qa_q2m_muzzle_offset(game->options.edition, flash);
  qa_vec3 origin = qa_add3_double(body.origin, qa_add3_double(
      qa_scale3_double(aim.right, offset.x + (flash == 85 ? -14 : 2)),
      qa_add3_double(qa_scale3_double(aim.up, offset.z + 8), qa_scale3_double(aim.forward, offset.y))));
  (void)qa_q2mpm_monster_dabeam(entity, game, entity->has_enemy ? &entity->enemy : NULL, origin, aim_angles, 1, false);
  state->hold_frame = q2mpm_now(game) < state->pause_time;
}
static void hyper_refire(qa_q2m_context *c, bool second) {
  if (c->entity->skin < 2 || c->entity->skin >= 4) return;
  if (q2mpm_rnd(c->game) < 0.7)
    c->entity->frame = second ? qa_q2mpm_xatrix_soldierh_frames_value.attak205
                              : qa_q2mpm_xatrix_soldierh_frames_value.attak103;
  else q2mpm_snd(c, "weapons/hyprbd1a.wav", 0);
}
static void walk_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2mpm_set_move(c, q2mpm_rnd(c->game) < 0.5 ? "soldierh_move_walk1" : "soldierh_move_walk2");
}
static void attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2mpm_set_move(c, c->entity->skin >= 4 ? "soldierh_move_attack4"
                                         : q2mpm_rnd(c->game) < 0.5 ? "soldierh_move_attack1" : "soldierh_move_attack2");
}
static void after_spawn_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->entity->max_health = qa_q2_integer_field(&c->entity->spawn, "health", 0);
}
static void sight_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2mpm_snd(c, q2mpm_rnd(c->game) < 0.5 ? "soldier/solsght1.wav" : "soldier/solsrch1.wav", 2);
  if (c->game->options.skill > 0 && qa_q2m_target_distance(c) >= 500 && q2mpm_rnd(c->game) > 0.5)
    q2mpm_set_move(c, c->entity->skin < 4 ? "soldierh_move_attack6" : "soldierh_move_attack4");
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx; (void)reaction;
  if (qa_q2m_health(c->game, &c->entity->actor.id) < c->entity->max_health / 2)
    c->entity->skin = q2mpm_or(c->entity->skin, 1);
  bool airborne = q2mpm_body(c->game, c->entity).velocity.z > 100;
  if (q2mpm_now(c->game) < c->state->pain_time) {
    const char *name = c->state->move->name;
    if (airborne && (strcmp(name, "soldierh_move_pain1") == 0 || strcmp(name, "soldierh_move_pain2") == 0
                     || strcmp(name, "soldierh_move_pain3") == 0))
      q2mpm_set_move(c, "soldierh_move_pain4");
    return;
  }
  c->state->pain_time = q2mpm_now(c->game) + 3;
  pain_sound(c, false);
  if (airborne) { q2mpm_set_move(c, "soldierh_move_pain4"); return; }
  if (c->game->options.skill == 3) return;
  double r = q2mpm_rnd(c->game);
  q2mpm_set_move(c, r < 0.33 ? "soldierh_move_pain1" : r < 0.66 ? "soldierh_move_pain2" : "soldierh_move_pain3");
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  if (qa_q2m_health(game, &entity->actor.id) <= state->gib_health) {
    q2mpm_snd(c, "misc/udeath.wav", 2);
    qa_q2m_gib_options plain = {false, false, false, false};
    for (int i = 0; i < 3; i++)
      (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_meat/tris.md2", reaction.pain.damage, plain);
    (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/chest/tris.md2", reaction.pain.damage, plain);
    (void)qa_q2m_throw_head(entity, game, "models/objects/gibs/head2/tris.md2", reaction.pain.damage);
    state->dead = true;
    state->gibbed = true;
    return;
  }
  if (state->dead) return;
  state->dead = true;
  state->can_take_damage = true;
  entity->skin = q2mpm_or(entity->skin, 1);
  q2mpm_set_damageable(game, entity, true);
  pain_sound(c, true);
  if (fabs(q2mpm_body(game, entity).origin.z + entity->view_height - reaction.point.z) <= 4) {
    q2mpm_set_move(c, "soldierh_move_death3");
    return;
  }
  static const char *const deaths[] = {"soldierh_move_death1", "soldierh_move_death2", "soldierh_move_death4",
                                       "soldierh_move_death5", "soldierh_move_death6"};
  q2mpm_set_move(c, q2mpm_at_s(deaths, 5, floor(q2mpm_rnd(game) * 5)));
}
static void dodge_fn(void *ctx, qa_q2m_context *c, qa_actor_id attacker, double eta, const qa_trace_result *trace,
                     bool gravity) {
  (void)ctx; (void)trace; (void)gravity;
  if (q2mpm_rnd(c->game) > 0.25) return;
  if (!c->entity->has_enemy) { c->entity->has_enemy = true; c->entity->enemy = attacker; }
  if (c->game->options.skill == 0) { q2mpm_set_move(c, "soldierh_move_duck"); return; }
  c->state->pause_time = q2mpm_now(c->game) + eta + 0.3;
  q2mpm_set_move(c, q2mpm_rnd(c->game) > (c->game->options.skill == 1 ? 0.33 : 0.66) ? "soldierh_move_duck"
                                                                                     : "soldierh_move_attack3");
}
static void idle_cb_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (q2mpm_rnd(c->game) > 0.8) q2mpm_snd4(c, "soldier/solidle1.wav", 2, 1, 2);
}
static void cock_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2mpm_snd4(c, "infantry/infatck3.wav", 1, 1,
             c->entity->frame == qa_q2mpm_xatrix_soldierh_frames_value.stand322 ? 2 : 1);
}
static void walk1_random_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (q2mpm_rnd(c->game) > 0.1) c->state->next_frame = qa_q2mpm_xatrix_soldierh_frames_value.walk101;
}
static void hyper_sound_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (c->entity->skin >= 2 && c->entity->skin < 4) q2mpm_snd(c, "weapons/hyprbl1a.wav", 0);
}
#define FIRE_FN(name, idx) static void name(void *ctx, qa_q2m_context *c) { fire_at(ctx, c, idx); }
FIRE_FN(fire1_fn, 0) FIRE_FN(fire2_fn, 1) FIRE_FN(fire4_fn, 3) FIRE_FN(fire8_fn, 7)
static void fire3_fn(void *ctx, qa_q2m_context *c) { duck_down(c); fire_at(ctx, c, 2); }
static void fire6_fn(void *ctx, qa_q2m_context *c) { if (c->entity->skin < 4) fire_at(ctx, c, 5); }
static void fire7_fn(void *ctx, qa_q2m_context *c) { if (c->entity->skin < 4) fire_at(ctx, c, 6); }
static void ripper1_fn(void *ctx, qa_q2m_context *c) { if (c->entity->skin < 4) fire_at(ctx, c, 0); }
static void ripper2_fn(void *ctx, qa_q2m_context *c) { if (c->entity->skin < 4) fire_at(ctx, c, 1); }
static void hyper1_fn(void *ctx, qa_q2m_context *c) { (void)ctx; hyper_refire(c, false); }
static void hyper2_fn(void *ctx, qa_q2m_context *c) { (void)ctx; hyper_refire(c, true); }
static void refire_a1_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  const qa_q2mpm_xatrix_soldierh_frames *f = &qa_q2mpm_xatrix_soldierh_frames_value;
  if (c->entity->skin <= 1 && qa_q2bm_alive_enemy(c))
    c->state->next_frame = refire_check(c) ? f->attak102 : f->attak110;
}
static void refire_a2_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (c->entity->skin >= 2 && qa_q2bm_alive_enemy(c) && refire_check(c))
    c->state->next_frame = qa_q2mpm_xatrix_soldierh_frames_value.attak102;
}
static void refire_b1_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  const qa_q2mpm_xatrix_soldierh_frames *f = &qa_q2mpm_xatrix_soldierh_frames_value;
  if (c->entity->skin <= 1 && qa_q2bm_alive_enemy(c))
    c->state->next_frame = refire_check(c) ? f->attak204 : f->attak216;
}
static void refire_b2_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (c->entity->skin >= 2 && qa_q2bm_alive_enemy(c)
      && ((c->game->options.skill == 3 && q2mpm_rnd(c->game) < 0.5)
          || (qa_q2m_target_distance(c) < 80 && c->entity->skin < 4)))
    c->state->next_frame = qa_q2mpm_xatrix_soldierh_frames_value.attak204;
}
static void refire3_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (q2mpm_now(c->game) + 0.4 < c->state->pause_time)
    c->state->next_frame = qa_q2mpm_xatrix_soldierh_frames_value.attak303;
}
static void refire6_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (qa_q2bm_alive_enemy(c) && qa_q2m_target_distance(c) >= 500 && c->game->options.skill == 3)
    c->state->next_frame = qa_q2mpm_xatrix_soldierh_frames_value.runs03;
}
static void duck_down_fn(void *ctx, qa_q2m_context *c) { (void)ctx; duck_down(c); }
static void duck_up_fn(void *ctx, qa_q2m_context *c) { (void)ctx; qa_q2m_set_duck(c, false); }
static void duck_hold_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->state->hold_frame = q2mpm_now(c->game) < c->state->pause_time;
}
static void skin2_fn(void *ctx, qa_q2m_context *c) { (void)ctx; c->entity->skin = 2; }
static void skin4_fn(void *ctx, qa_q2m_context *c) { (void)ctx; c->entity->skin = 4; }

size_t qa_q2mpm_create_soldier_heavy_definitions(qa_q2mpm_arena *arena, qa_q2m_definition *out, qa_q2mpm_weapons weapons) {
  qa_q2mpm_weapons *box = qa_q2mpm_arena_alloc(arena, sizeof *box);
  if (!box) qa_raise("Error", 0, 0, "Out of memory creating soldier heavy definitions");
  *box = weapons;
  qa_q2m_named_handler *callbacks = qa_q2mpm_arena_alloc(arena, 27 * sizeof *callbacks);
  if (!callbacks) qa_raise("Error", 0, 0, "Out of memory creating soldier heavy definitions");
  size_t n = 0;
  callbacks[n++] = (qa_q2m_named_handler){"soldierh_stand", {NULL, stand_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"soldierh_run", {NULL, run_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"soldierh_dead", {NULL, qa_q2bm_finish_corpse_handler}};
  callbacks[n++] = (qa_q2m_named_handler){"soldierh_idle", {NULL, idle_cb_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"soldierh_cock", {NULL, cock_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"soldierh_walk1_random", {NULL, walk1_random_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"soldierh_hyper_sound", {NULL, hyper_sound_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"soldierh_hyper_refire1", {NULL, hyper1_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"soldierh_hyper_refire2", {NULL, hyper2_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"soldierh_fire1", {box, fire1_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"soldierh_fire2", {box, fire2_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"soldierh_fire3", {box, fire3_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"soldierh_fire4", {box, fire4_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"soldierh_fire8", {box, fire8_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"soldierh_fire6", {box, fire6_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"soldierh_fire7", {box, fire7_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"soldierh_ripper1", {box, ripper1_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"soldierh_ripper2", {box, ripper2_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"soldierh_attack1_refire1", {NULL, refire_a1_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"soldierh_attack1_refire2", {NULL, refire_a2_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"soldierh_attack2_refire1", {NULL, refire_b1_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"soldierh_attack2_refire2", {NULL, refire_b2_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"soldierh_attack3_refire", {NULL, refire3_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"soldierh_attack6_refire", {NULL, refire6_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"soldierh_duck_down", {NULL, duck_down_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"soldierh_duck_up", {NULL, duck_up_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"soldierh_duck_hold", {NULL, duck_hold_fn}};
  qa_q2m_definition common;
  memset(&common, 0, sizeof common);
  common.classname = "monster_soldier_ripper";
  common.kind = "soldierh";
  common.model = "models/monsters/soldierh/tris.md2";
  common.health = 50;
  common.gib_health = -30;
  common.mass = 100;
  common.bounds = qa_q2bm_humanoid_bounds;
  common.scale = 1;
  common.initial_move = "soldierh_move_stand3";
  common.moves = qa_q2mpm_xatrix_soldierh_moves;
  common.move_count = qa_q2mpm_xatrix_soldierh_move_count;
  common.callbacks = callbacks;
  common.callback_count = n;
  common.stand = (qa_q2m_handler){NULL, stand_fn};
  common.walk = (qa_q2m_handler){NULL, walk_fn};
  common.run = (qa_q2m_handler){NULL, run_fn};
  common.attack = (qa_q2m_handler){NULL, attack_fn};
  common.sight = (qa_q2m_handler){NULL, sight_fn};
  common.after_spawn = (qa_q2m_handler){NULL, after_spawn_fn};
  common.reactions.pain = pain_fn;
  common.reactions.die = die_fn;
  common.dodge = dodge_fn;
  out[0] = common;
  out[1] = common;
  out[1].classname = "monster_soldier_hypergun";
  out[1].health = 60;
  out[1].initialize = (qa_q2m_handler){NULL, skin2_fn};
  out[2] = common;
  out[2].classname = "monster_soldier_lasergun";
  out[2].health = 70;
  out[2].initialize = (qa_q2m_handler){NULL, skin4_fn};
  return 3;
}
