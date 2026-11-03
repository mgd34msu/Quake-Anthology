/* Port of src/content/q2/rerelease/monsters/base-variants/parasite.ts.
 * Rerelease m_parasite.cpp. ZeniMax Media, GPL-2.0. */
#include "qa/content/q2/rerelease/monsters/base_variants/parasite.h"

#include <stdlib.h>

#include "qa/content/q2/base/monsters/parasite.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/gibs.h"
#include "qa/content/q2/rerelease/monsters/base_variants/proboscis.h"
#include "qa/content/q2/rerelease/monsters/common.h"
#include "qa/content/q2/rerelease/monsters/tables/parasite.h"
#include "../rrm_internal.h"

static void start_run_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2rm_set_move(c, c->state->stand_ground ? "parasite_move_stand" : "parasite_move_start_run");
}
static void jump_impl(qa_q2m_context *c, bool up) {
  qa_body_state body = q2rm_body(c->game, c->entity);
  qa_angle_vectors axes = qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE);
  qa_vec3 velocity = qa_add3_double(body.velocity,
      qa_add3_double(qa_scale3_double(axes.forward, up ? 200 : 100), qa_scale3_double(axes.up, up ? 450 : 300)));
  qa_q2_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_velocity = true; change.velocity = velocity;
  q2rm_move_body(c->game, c->entity, change, false, false);
}
static void retract(qa_q2rm_proboscis *proboscis, qa_q2m_context *c) {
  qa_q2_entity *tip = c->game->entity(c->game->ctx, c->entity->has_proboscus ? &c->entity->proboscus : NULL);
  if (tip && tip->style != 2) qa_q2rm_proboscis_retract(proboscis, tip, c->game);
}
static void run_fn(void *ctx, qa_q2m_context *c) {
  retract(ctx, c);
  q2rm_set_move(c, c->state->stand_ground ? "parasite_move_stand" : "parasite_move_run");
}
static void init_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->state->yaw_speed = 30;
}
static void idle_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (!c->entity->has_enemy) q2rm_set_move(c, "parasite_move_start_fidget");
}
static void attack_fn(void *ctx, qa_q2m_context *c) {
  if (!qa_q2m_clear_shot(c, q2rm_v3(-1.7, 0, 1.2))) return;
  retract(ctx, c);
  q2rm_set_move(c, "parasite_move_fire_proboscis");
}
static void charge_ai_fn(void *ctx, qa_q2m_context *c, double distance) {
  qa_q2rm_proboscis *proboscis = ctx;
  double frame = c->entity->frame;
  bool breaking = frame >= qa_q2rm_parasite_frames_value.break01 && frame <= qa_q2rm_parasite_frames_value.break32;
  qa_q2m_run_ai(c, breaking ? QA_Q2M_AI_MOVE : QA_Q2M_AI_CHARGE, distance);
  qa_q2_entity *tip = c->game->entity(c->game->ctx, c->entity->has_proboscus ? &c->entity->proboscus : NULL);
  qa_q2_entity *segment = tip ? c->game->entity(c->game->ctx, tip->has_proboscus ? &tip->proboscus : NULL) : NULL;
  if (segment) proboscis->draw.fn(proboscis->draw.ctx, segment, c->game);
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)reaction;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  entity->skin = qa_q2m_health(game, &entity->actor.id) < entity->max_health / 2 ? 1 : 0;
  if (q2rm_now(game) < state->pain_time) return;
  retract(ctx, c);
  state->pain_time = q2rm_now(game) + 3;
  q2rm_snd(c, q2rm_rnd(game) < 0.5 ? "parasite/parpain1.wav" : "parasite/parpain2.wav", 2);
  if (qa_q2rm_reacts_to_pain(c)) q2rm_set_move(c, "parasite_move_pain1");
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  qa_q2rm_proboscis *proboscis = ctx;
  qa_q2_game_services *game = c->game;
  qa_q2_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  double damage = reaction.pain.damage;
  qa_q2_entity *tip = game->entity(game->ctx, entity->has_proboscus ? &entity->proboscus : NULL);
  if (tip && tip->style != 2) proboscis->reset.fn(proboscis->reset.ctx, tip, game);
  if (qa_q2rm_check_gib(c)) {
    q2rm_snd(c, "misc/udeath.wav", 2);
    entity->skin = trunc(entity->skin / 2);
    qa_q2m_gib_options plain = {false, false, false, false};
    qa_q2m_gib_options skinned = {false, false, true, false};
    qa_q2m_gib_options skin_up = {false, false, true, true};
    qa_q2m_gib_options head = {true, false, true, false};
    (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/bone/tris.md2", damage, plain);
    for (int i = 0; i < 3; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_meat/tris.md2", damage, plain);
    (void)qa_q2m_throw_gib(entity, game, "models/monsters/parasite/gibs/chest.md2", damage, skinned);
    static const char *const legs[4] = {
      "models/monsters/parasite/gibs/bleg.md2", "models/monsters/parasite/gibs/bleg.md2",
      "models/monsters/parasite/gibs/fleg.md2", "models/monsters/parasite/gibs/fleg.md2"};
    for (int i = 0; i < 4; i++) (void)qa_q2m_throw_gib(entity, game, legs[i], damage, skin_up);
    (void)qa_q2m_throw_gib(entity, game, "models/monsters/parasite/gibs/head.md2", damage, head);
    state->dead = true; state->gibbed = true;
    return;
  }
  if (state->dead) return;
  q2rm_snd(c, "parasite/pardeth1.wav", 2);
  state->dead = true; state->can_take_damage = true;
  q2rm_set_damageable(game, entity, true);
  q2rm_set_move(c, "parasite_move_death");
}
static bool blocked_fn(void *ctx, qa_q2m_context *c, double distance) {
  (void)ctx;
  qa_q2rm_jump_navigation navigation;
  memset(&navigation, 0, sizeof navigation);
  qa_q2rm_jump_result result = qa_q2rm_blocked_check_jump(c, distance, 256, 68,
                                                          !q2rm_has(c->entity->spawnflags, 8), navigation);
  if (result != QA_Q2RM_JUMP_NONE) {
    if (result != QA_Q2RM_JUMP_TURN && c->entity->has_enemy)
      q2rm_set_move(c, result == QA_Q2RM_JUMP_UP ? "parasite_move_jump_up" : "parasite_move_jump_down");
    return true;
  }
  return qa_q2rm_blocked_check_platform(c, distance);
}
static void tap_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2rm_snd4(c, "parasite/paridle1.wav", 1, 0.75, 2.75);
}
static void scratch_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2rm_snd4(c, "parasite/paridle2.wav", 1, 0.75, 2.75);
}
static void fire_fn(void *ctx, qa_q2m_context *c) { qa_q2rm_proboscis_fire(ctx, c); }
static void wait_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->state->next_frame = c->entity->frame == qa_q2rm_parasite_frames_value.drain04
      ? qa_q2rm_parasite_frames_value.drain05 : qa_q2rm_parasite_frames_value.drain04;
}
static void pull_wait_fn(void *ctx, qa_q2m_context *c) {
  qa_q2_entity *tip = c->game->entity(c->game->ctx, c->entity->has_proboscus ? &c->entity->proboscus : NULL);
  if (!tip || tip->style == 3) c->state->next_frame = qa_q2rm_parasite_frames_value.drain14;
  else {
    c->state->next_frame = c->entity->frame == qa_q2rm_parasite_frames_value.drain12
        ? qa_q2rm_parasite_frames_value.drain13 : qa_q2rm_parasite_frames_value.drain12;
    if (tip->style != 2) qa_q2rm_proboscis_retract(ctx, tip, c->game);
  }
}
static void break_noise_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2rm_snd(c, "parasite/parsrch1.wav", 2);
}
static void break_retract_fn(void *ctx, qa_q2m_context *c) {
  qa_q2_entity *tip = c->game->entity(c->game->ctx, c->entity->has_proboscus ? &c->entity->proboscus : NULL);
  if (tip) qa_q2rm_proboscis_retract(ctx, tip, c->game);
}
static void break_sound_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->state->pain_time = q2rm_now(c->game) + 3;
  q2rm_snd(c, q2rm_rnd(c->game) < 0.5 ? "parasite/parpain1.wav" : "parasite/parpain2.wav", 2);
}
static void break_wait_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2_entity *tip = c->game->entity(c->game->ctx, c->entity->has_proboscus ? &c->entity->proboscus : NULL);
  if (tip && tip->style != 3) c->state->next_frame = qa_q2rm_parasite_frames_value.break19;
  else if (qa_q2_rerelease_random_integer_max(qa_q2rm_rerelease_random(c), 2) != 0) {
    q2rm_snd(c, "parasite/paratck4.wav", 1);
    c->state->next_frame = qa_q2rm_parasite_frames_value.break31;
  }
}
static void jump_down_fn(void *ctx, qa_q2m_context *c) { (void)ctx; jump_impl(c, false); }
static void jump_up_fn(void *ctx, qa_q2m_context *c) { (void)ctx; jump_impl(c, true); }
static void wait_land_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->state->next_frame = q2rm_body(c->game, c->entity).ground || qa_q2rm_monster_jump_finished(c)
      ? c->entity->frame + 1 : c->entity->frame;
}
static void shrink_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_bounds bounds = q2rm_body(c->game, c->entity).bounds;
  c->entity->server_flags = q2rm_or(c->entity->server_flags, 2);
  bounds.max.z = 0;
  qa_q2_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_bounds = true; change.bounds = bounds;
  q2rm_move_body(c->game, c->entity, change, false, false);
}
static void dead_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_bounds = true;
  change.bounds.min = q2rm_v3(-16, -16, -24);
  change.bounds.max = q2rm_v3(16, 16, -8);
  q2rm_move_body(c->game, c->entity, change, false, false);
  qa_q2m_corpse(c);
}

qa_q2m_definition qa_q2rm_create_rerelease_parasite_definition(qa_q2mpm_arena *arena, qa_q2m_monsters *monsters) {
  qa_q2rm_proboscis *proboscis = qa_q2mpm_arena_alloc(arena, sizeof *proboscis);
  qa_q2m_named_ai *ai = qa_q2mpm_arena_alloc(arena, sizeof *ai);
  if (!proboscis || !ai) qa_raise("Error", 0, 0, "Out of memory creating rerelease parasite definition");
  qa_q2rm_create_proboscis(proboscis, monsters);
  ai[0] = (qa_q2m_named_ai){"parasite_charge_proboscis", proboscis, charge_ai_fn};
  const qa_q2m_named_handler extra[] = {
    {"parasite_run", {proboscis, run_fn}},
    {"parasite_start_run", {NULL, start_run_fn}},
    {"parasite_tap", {NULL, tap_fn}},
    {"parasite_scratch", {NULL, scratch_fn}},
    {"parasite_fire_proboscis", {proboscis, fire_fn}},
    {"parasite_proboscis_wait", {NULL, wait_fn}},
    {"parasite_proboscis_pull_wait", {proboscis, pull_wait_fn}},
    {"parasite_break_noise", {NULL, break_noise_fn}},
    {"parasite_break_retract", {proboscis, break_retract_fn}},
    {"parasite_break_sound", {NULL, break_sound_fn}},
    {"parasite_break_wait", {NULL, break_wait_fn}},
    {"parasite_jump_down", {NULL, jump_down_fn}},
    {"parasite_jump_up", {NULL, jump_up_fn}},
    {"parasite_jump_wait_land", {NULL, wait_land_fn}},
    {"parasite_shrink", {NULL, shrink_fn}},
    {"parasite_dead", {NULL, dead_fn}},
  };
  qa_q2m_definition definition = qa_q2bm_parasite_definition;
  definition.moves = qa_q2rm_parasite_moves;
  definition.move_count = qa_q2rm_parasite_move_count;
  definition.has_source_callbacks = true;
  definition.source_callbacks = qa_q2rm_proboscis_callbacks(proboscis);
  definition.run = (qa_q2m_handler){NULL, start_run_fn};
  definition.initialize = (qa_q2m_handler){NULL, init_fn};
  definition.idle = (qa_q2m_handler){NULL, idle_fn};
  definition.attack = (qa_q2m_handler){proboscis, attack_fn};
  definition.ai = ai;
  definition.ai_count = 1;
  definition.reactions.ctx = proboscis;
  definition.reactions.pain = pain_fn;
  definition.reactions.die = die_fn;
  definition.blocked_ctx = NULL;
  definition.blocked = blocked_fn;
  definition.callbacks = qa_q2rm_merge_callbacks(arena, definition.callbacks, definition.callback_count,
                                                 extra, sizeof extra / sizeof extra[0], &definition.callback_count);
  return definition;
}
