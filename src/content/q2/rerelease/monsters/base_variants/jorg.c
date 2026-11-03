/* Port of src/content/q2/rerelease/monsters/base-variants/jorg.ts.
 * Rerelease m_boss31.cpp. ZeniMax Media, GPL-2.0. */
#include "qa/content/q2/rerelease/monsters/base_variants/jorg.h"

#include <stdlib.h>

#include "qa/content/q2/base/monsters/boss_common.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/gibs.h"
#include "qa/content/q2/foundation/monsters/muzzle.h"
#include "qa/content/q2/foundation/monsters/perception.h"
#include "qa/content/q2/rerelease/monsters/base_variants/makron.h"
#include "qa/content/q2/rerelease/monsters/boss.h"
#include "qa/content/q2/rerelease/monsters/common.h"
#include "qa/content/q2/rerelease/monsters/tables/boss31.h"
#include "../rrm_internal.h"

typedef struct jorg_ctx {
  qa_q2m_monsters *monsters;
  qa_q2rm_healthbar_transfer transfer;
} jorg_ctx;

static void end_sound(qa_q2m_context *c) {
  if (c->entity->sound && strcmp(c->entity->sound, "") != 0) {
    q2rm_snd(c, "boss3/bs3atck1_end.wav", 1);
    c->entity->sound = "";
    qa_q2bm_stop_loop(c);
  }
}
static void end_sound_fn(void *ctx, qa_q2m_context *c) { (void)ctx; end_sound(c); }
static void run_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2rm_set_move(c, c->state->stand_ground ? "jorg_move_stand" : "jorg_move_run");
  end_sound(c);
}
static void stand_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2rm_set_move(c, "jorg_move_stand");
  end_sound(c);
}
static void bullet(qa_q2m_context *c, double flash, double offset) {
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(QA_Q2_EDITION_RERELEASE, flash));
  qa_vec3 direction;
  if (!qa_q2rm_predicted_direction(c, start, 0, false, offset, &direction)) return;
  c->weapons.fire_bullet(c->weapons.ctx, c->entity, c->game, start, direction, 6, 4, 300, 500, 0);
  qa_q2rm_monster_flash(c, flash, start, direction);
}
static void toss(jorg_ctx *box, qa_q2m_context *c) {
  qa_actor_id spawned = qa_q2rm_toss_rerelease_makron(c, box->monsters);
  if (box->transfer.fn) box->transfer.fn(box->transfer.ctx, c->entity->actor.id, spawned, c->game);
}
static void toss_fn(void *ctx, qa_q2m_context *c) { toss(ctx, c); }
static void init_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->entity->model2 = "models/monsters/boss3/rider/tris.md2";
  c->state->ignore_shots = true;
}
static bool check_attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2m_attack_chance_profile profile = {0.4, 0.8, 0.4, 0.2, 0, 0};
  return qa_q2m_default_check_attack(c, &profile);
}
static void attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (q2rm_rnd(c->game) <= 0.75) {
    q2rm_snd(c, "boss3/bs3atck1.wav", 1);
    c->entity->sound = "boss3/w_loop.wav";
    qa_q2bm_loop_sound(c, c->entity->sound);
    q2rm_set_move(c, "jorg_move_start_attack1");
    return;
  }
  q2rm_snd(c, "boss3/bs3atck2.wav", 2);
  q2rm_set_move(c, "jorg_move_attack2");
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  double frame = entity->frame;
  const qa_q2rm_boss31_frames *f = &qa_q2rm_boss31_frames_value;
  entity->skin = qa_q2m_health(game, &entity->actor.id) < entity->max_health / 2 ? 1 : 0;
  if (q2rm_now(game) < state->pain_time) return;
  if (!qa_q2rm_chainfist(c)) {
    if (reaction.damage <= 40 && q2rm_rnd(game) <= 0.6) return;
    if (frame >= f->attak101 && frame <= f->attak108 && q2rm_rnd(game) <= 0.005) return;
    if (frame >= f->attak109 && frame <= f->attak114 && q2rm_rnd(game) <= 0.00005) return;
    if (frame >= f->attak201 && frame <= f->attak208 && q2rm_rnd(game) <= 0.005) return;
  }
  state->pain_time = q2rm_now(game) + 3;
  bool heavy = false;
  if (reaction.damage > 50 && reaction.damage <= 100) q2rm_snd(c, "boss3/bs3pain2.wav", 2);
  else if (reaction.damage > 100 && q2rm_rnd(game) <= 0.3) {
    heavy = true;
    q2rm_snd(c, "boss3/bs3pain3.wav", 2);
  }
  if (!qa_q2rm_reacts_to_pain(c)) return;
  end_sound(c);
  if (reaction.damage <= 50) q2rm_set_move(c, "jorg_move_pain1");
  else if (reaction.damage <= 100) q2rm_set_move(c, "jorg_move_pain2");
  else if (heavy) q2rm_set_move(c, "jorg_move_pain3");
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx; (void)reaction;
  q2rm_snd(c, "boss3/bs3deth1.wav", 2);
  end_sound(c);
  c->state->dead = true;
  c->state->can_take_damage = false;
  c->entity->count = 0;
  q2rm_set_damageable(c->game, c->entity, false);
  q2rm_set_move(c, "jorg_move_death");
}
static void reattack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (qa_q2m_visible_enemy(c) && q2rm_rnd(c->game) < 0.9) { q2rm_set_move(c, "jorg_move_attack1"); return; }
  q2rm_set_move(c, "jorg_move_end_attack1");
  end_sound(c);
}
static void bfg_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2bm_shot_result aim;
  if (!qa_q2bm_shot_default(c, 132, &aim)) return;
  q2rm_snd(c, "makron/bfg_fire.wav", 1);
  (void)c->weapons.fire_bfg(c->weapons.ctx, c->entity, c->game, aim.start, aim.direction, 50, 300, 200);
  qa_q2rm_monster_flash(c, 132, aim.start, aim.direction);
}
static void bullet_left_fn(void *ctx, qa_q2m_context *c) { (void)ctx; bullet(c, 120, 0.2); }
static void bullet_right_fn(void *ctx, qa_q2m_context *c) { (void)ctx; bullet(c, 126, -0.2); }
static void firebullet_fn(void *ctx, qa_q2m_context *c) { (void)ctx; bullet(c, 120, 0.2); bullet(c, 126, -0.2); }
static void dead_fn(void *ctx, qa_q2m_context *c) {
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  q2rm_emit_effect(game, "q2:explosion1-big", q2rm_body(game, entity).origin, q2rm_zero(), 1, 0);
  qa_q2bm_stop_loop(c);
  entity->sound = "";
  entity->skin = trunc(entity->skin / 2);
  qa_q2m_gib_options plain = {false, false, false, false};
  qa_q2m_gib_options metal = {false, true, false, false};
  qa_q2m_gib_options skinned = {false, false, true, false};
  qa_q2m_gib_options skin_up = {false, false, true, true};
  qa_q2m_gib_options head = {true, true, true, false};
  for (int i = 0; i < 2; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_meat/tris.md2", 500, plain);
  for (int i = 0; i < 2; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_metal/tris.md2", 500, metal);
  static const char *const body_parts[13] = {
    "models/monsters/boss3/jorg/gibs/chest.md2", "models/monsters/boss3/jorg/gibs/foot.md2",
    "models/monsters/boss3/jorg/gibs/foot.md2", "models/monsters/boss3/jorg/gibs/tube.md2",
    "models/monsters/boss3/jorg/gibs/tube.md2", "models/monsters/boss3/jorg/gibs/tube.md2",
    "models/monsters/boss3/jorg/gibs/tube.md2", "models/monsters/boss3/jorg/gibs/spike.md2",
    "models/monsters/boss3/jorg/gibs/spike.md2", "models/monsters/boss3/jorg/gibs/spike.md2",
    "models/monsters/boss3/jorg/gibs/spike.md2", "models/monsters/boss3/jorg/gibs/spike.md2",
    "models/monsters/boss3/jorg/gibs/spike.md2"};
  for (int i = 0; i < 13; i++) (void)qa_q2m_throw_gib(entity, game, body_parts[i], 500, skinned);
  static const char *const up_parts[5] = {
    "models/monsters/boss3/jorg/gibs/gun.md2", "models/monsters/boss3/jorg/gibs/gun.md2",
    "models/monsters/boss3/jorg/gibs/thigh.md2", "models/monsters/boss3/jorg/gibs/thigh.md2",
    "models/monsters/boss3/jorg/gibs/spine.md2"};
  for (int i = 0; i < 5; i++) (void)qa_q2m_throw_gib(entity, game, up_parts[i], 500, skin_up);
  (void)qa_q2m_throw_gib(entity, game, "models/monsters/boss3/jorg/gibs/head.md2", 500, head);
  state->gibbed = true;
  toss(ctx, c);
}

void qa_q2rm_create_rerelease_jorg_definition(qa_q2mpm_arena *arena, qa_q2rm_jorg_definition *out, qa_q2m_monsters *monsters,
                                              qa_q2rm_healthbar_transfer transfer) {
  memset(out, 0, sizeof *out);
  qa_q2bm_create_jorg_definition(&out->base, monsters);
  jorg_ctx *box = qa_q2mpm_arena_alloc(arena, sizeof *box);
  if (!box) qa_raise("Error", 0, 0, "Out of memory creating rerelease jorg definition");
  box->monsters = monsters;
  box->transfer = transfer;
  qa_q2m_definition definition = out->base.definition;
  const qa_q2m_named_handler extra[] = {
    {"jorg_run", {NULL, run_fn}},
    {"BossExplode", {NULL, qa_q2rm_boss_explode_handler}},
    {"jorg_reattack1", {NULL, reattack_fn}},
    {"jorg_attack1_end_sound", {NULL, end_sound_fn}},
    {"jorgBFG", {NULL, bfg_fn}},
    {"jorg_firebullet_left", {NULL, bullet_left_fn}},
    {"jorg_firebullet_right", {NULL, bullet_right_fn}},
    {"jorg_firebullet", {NULL, firebullet_fn}},
    {"MakronToss", {box, toss_fn}},
    {"jorg_dead", {box, dead_fn}},
  };
  size_t count = 0;
  for (size_t i = 0; i < definition.callback_count; i++) out->callbacks[count++] = definition.callbacks[i];
  for (size_t i = 0; i < sizeof extra / sizeof extra[0]; i++) {
    size_t j = 0;
    while (j < count && strcmp(out->callbacks[j].name, extra[i].name) != 0) j++;
    if (j < count) out->callbacks[j] = extra[i];
    else {
      if (count >= 24) qa_raise("Error", 0, 0, "Rerelease jorg callback table overflow");
      out->callbacks[count++] = extra[i];
    }
  }
  definition.moves = qa_q2rm_boss31_moves;
  definition.move_count = qa_q2rm_boss31_move_count;
  definition.model = "models/monsters/boss3/jorg/tris.md2";
  definition.health = 8000;
  definition.run = (qa_q2m_handler){NULL, run_fn};
  out->think_defs[0].name = "BossExplode_think";
  out->think_defs[0].cb = qa_q2rm_boss_explode_think();
  definition.has_source_callbacks = true;
  memset(&definition.source_callbacks, 0, sizeof definition.source_callbacks);
  definition.source_callbacks.think = out->think_defs;
  definition.source_callbacks.think_count = 1;
  definition.initialize = (qa_q2m_handler){NULL, init_fn};
  definition.stand = (qa_q2m_handler){NULL, stand_fn};
  definition.check_attack_ctx = NULL;
  definition.check_attack = check_attack_fn;
  definition.attack = (qa_q2m_handler){NULL, attack_fn};
  definition.reactions.pain = pain_fn;
  definition.reactions.die = die_fn;
  definition.callbacks = out->callbacks;
  definition.callback_count = count;
  out->definition = definition;
}
