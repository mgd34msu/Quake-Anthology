/* Port of src/content/q2/base/monsters/common.ts. GPL-2.0-or-later. */
#include "qa/content/q2/base/monsters/common.h"
#include "qa/foundation/js.h"

#include "base_internal.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/gibs.h"
#include "qa/content/q2/foundation/monsters/muzzle.h"

const qa_bounds qa_q2bm_humanoid_bounds = {{-16, -16, -24}, {16, 16, 32}};

void qa_q2bm_move_handler_fn(void *ctx, qa_q2m_context *context) { q2bm_set_move(context, ctx); }
qa_q2m_handler qa_q2bm_move_handler(const char *name) {
  qa_q2m_handler h = {(void *)name, qa_q2bm_move_handler_fn};
  return h;
}
void qa_q2bm_sound_handler_fn(void *ctx, qa_q2m_context *context) {
  const qa_q2bm_sound_spec *spec = ctx;
  q2bm_sound(context->game, context->entity, spec->path, true, spec->channel, true, 1, true, spec->attenuation);
}
qa_q2m_handler qa_q2bm_sound_handler(const qa_q2bm_sound_spec *spec) {
  qa_q2m_handler h = {(void *)spec, qa_q2bm_sound_handler_fn};
  return h;
}
bool qa_q2bm_alive_enemy(qa_q2m_context *context) {
  qa_body_state body;
  return qa_q2m_enemy_body(context, &body)
      && qa_q2m_health(context->game, context->entity->has_enemy ? &context->entity->enemy : NULL) > 0;
}
void qa_q2bm_damaged_skin(qa_q2m_context *context) {
  if (qa_q2m_health(context->game, &context->entity->actor.id) < context->entity->max_health / 2)
    context->entity->skin = 1;
}
void qa_q2bm_muzzle(qa_q2m_context *context, double flash, qa_vec3 direction, qa_vec3 origin) {
  qa_q2_presentation_event e;
  memset(&e, 0, sizeof e);
  e.kind = "monster-muzzleflash"; e.has_actor = true; e.actor = context->entity->actor.id;
  e.flash = flash; e.origin = origin; e.direction = direction;
  context->game->host->emit(context->game->host->ctx, e);
}
bool qa_q2bm_shot(qa_q2m_context *context, double flash, double lead, qa_q2bm_shot_result *out) {
  qa_body_state enemy;
  qa_vec3 eye;
  if (!qa_q2m_enemy_body(context, &enemy) || !qa_q2m_enemy_eye(context, &eye)) return false;
  out->start = qa_q2m_project_flash_default(context, qa_q2m_muzzle_offset(context->game->options.edition, flash));
  out->direction = qa_normalize3_double_hypot(
      qa_sub3_double(qa_add3_double(eye, qa_scale3_double(enemy.velocity, lead)), out->start));
  return true;
}
bool qa_q2bm_shot_default(qa_q2m_context *context, double flash, qa_q2bm_shot_result *out) {
  return qa_q2bm_shot(context, flash, 0, out);
}
qa_q2bm_shot_result qa_q2bm_forward_shot(qa_q2m_context *context, double flash) {
  qa_q2bm_shot_result r;
  r.start = qa_q2m_project_flash_default(context, qa_q2m_muzzle_offset(context->game->options.edition, flash));
  r.direction = qa_math_angle_vectors_rule(q2bm_body(context->game, context->entity).angles, QA_ANGLE_RULE_NATIVE).forward;
  return r;
}
bool qa_q2bm_standard_gib(qa_q2m_context *context, qa_death_reaction reaction, double bones, double meats,
                          const char *head, double attenuation) {
  qa_q2m_state *state = context->state;
  qa_q2_game_services *game = context->game;
  qa_q2_entity *entity = context->entity;
  if (qa_q2m_health(game, &entity->actor.id) > state->gib_health) return false;
  q2bm_sound(game, entity, "misc/udeath.wav", true, 2, true, 1, true, attenuation);
  qa_q2m_gib_options plain = {false, false, false, false};
  for (double i = 0; i < bones; i++)
    (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/bone/tris.md2", reaction.pain.damage, plain);
  for (double i = 0; i < meats; i++)
    (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_meat/tris.md2", reaction.pain.damage, plain);
  (void)qa_q2m_throw_head(entity, game, head, reaction.pain.damage);
  state->dead = true; state->gibbed = true;
  return true;
}
bool qa_q2bm_standard_gib_default(qa_q2m_context *context, qa_death_reaction reaction) {
  return qa_q2bm_standard_gib(context, reaction, 2, 4, "models/objects/gibs/head2/tris.md2", 1);
}
void qa_q2bm_explode(qa_q2m_context *context, const char *path) {
  qa_q2_entity *entity = context->entity;
  qa_q2_game_services *game = context->game;
  q2bm_sound(game, entity, path, true, 2, false, 0, false, 0);
  q2bm_emit_effect(game, "q2:explosion1", q2bm_body(game, entity).origin, q2bm_zero(), 1, 0);
  context->state->dead = true;
  game->remove(game->ctx, entity);
}
void qa_q2bm_loop_sound(qa_q2m_context *context, const char *path) {
  q2bm_emit_loop(context->game, context->entity, path, "start", 1);
}
void qa_q2bm_begin_death(qa_q2m_context *context, qa_death_reaction reaction, const char *path,
                         const char *animation, double bones, double meats) {
  if (qa_q2bm_standard_gib(context, reaction, bones, meats, "models/objects/gibs/head2/tris.md2", 1)
      || context->state->dead)
    return;
  q2bm_sound(context->game, context->entity, path, true, 2, false, 0, false, 0);
  context->state->dead = true; context->state->can_take_damage = true;
  q2bm_set_traits_damage(context->game, context->entity, true);
  q2bm_set_move(context, animation);
}
void qa_q2bm_begin_death_default(qa_q2m_context *context, qa_death_reaction reaction, const char *path,
                                 const char *animation) {
  qa_q2bm_begin_death(context, reaction, path, animation, 2, 4);
}
void qa_q2bm_finish_corpse_bounds(qa_q2m_context *context, qa_bounds bounds) {
  qa_q2_entity *entity = context->entity;
  qa_q2_game_services *game = context->game;
  qa_q2m_state *state = context->state;
  state->corpse = true;
  entity->server_flags = (double)(qa_js_to_int32(qa_js_to_uint32(entity->server_flags) | 2u));
  qa_q2_body_changes changes;
  memset(&changes, 0, sizeof changes);
  changes.has_bounds = true; changes.bounds = bounds;
  q2bm_move_body(game, entity, changes, false, false);
  game->motion(game->ctx, entity, "toss");
  game->cancel(game->ctx, entity);
}
void qa_q2bm_finish_corpse(qa_q2m_context *context) {
  qa_bounds bounds = {{-16, -16, -24}, {16, 16, -8}};
  qa_q2bm_finish_corpse_bounds(context, bounds);
}
void qa_q2bm_finish_corpse_handler(void *ctx, qa_q2m_context *context) {
  (void)ctx;
  qa_q2bm_finish_corpse(context);
}
