/* Port of src/content/q2/base/monsters/boss3-stand.ts. GPL-2.0-or-later. */
#include "qa/content/q2/base/monsters/boss3_stand.h"

#include "base_internal.h"
#include "qa/content/q2/base/monsters/tables/boss32.h"

static void think_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game) {
  (void)ctx;
  entity->frame = entity->frame == qa_q2bm_boss32_frames_value.stand260
      ? qa_q2bm_boss32_frames_value.stand201 : entity->frame + 1;
  game->show(game->ctx, entity);
  qa_q2f_think think = {NULL, think_fn};
  game->schedule(game->ctx, entity, 0.1, think);
}
static void use_fn(void *ctx, qa_q2f_entity *self, qa_q2f_game *game,
                   const qa_actor_id *other, const qa_actor_id *activator) {
  (void)ctx; (void)other; (void)activator;
  q2bm_emit_effect(game, "q2:boss-teleport", q2bm_body(game, self).origin, q2bm_zero(), 1, 0);
  game->remove(game->ctx, self);
}
static const qa_q2f_named_think think_defs[] = {{"q2:base/Think_Boss3Stand", {NULL, think_fn}}};
static const qa_q2f_named_use use_defs[] = {{"q2:base/Use_Boss3", {NULL, use_fn}}};
static qa_q2f_callback_definitions callbacks_fn(void *ctx) {
  (void)ctx;
  qa_q2f_callback_definitions defs;
  memset(&defs, 0, sizeof defs);
  defs.think = think_defs; defs.think_count = 1;
  defs.use = use_defs; defs.use_count = 1;
  return defs;
}
static bool spawn_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game) {
  (void)ctx;
  if (strcmp(entity->classname ? entity->classname : "", "monster_boss3_stand") != 0) return false;
  if (game->options.mode && strcmp(game->options.mode, "deathmatch") == 0) {
    game->remove(game->ctx, entity);
    return true;
  }
  entity->model = "models/monsters/boss3/rider/tris.md2";
  entity->frame = qa_q2bm_boss32_frames_value.stand201;
  qa_q2f_body_changes changes;
  memset(&changes, 0, sizeof changes);
  changes.has_bounds = true;
  changes.bounds = (qa_bounds){{-32, -32, 0}, {32, 32, 90}};
  game->move(game->ctx, entity, changes, false, false);
  game->motion(game->ctx, entity, QA_Q2F_MOTION_STEP);
  game->solid(game->ctx, entity, QA_Q2F_SOLID_BOX);
  game->show(game->ctx, entity);
  entity->use = (qa_q2f_use){NULL, use_fn};
  qa_q2f_think think = {NULL, think_fn};
  game->schedule(game->ctx, entity, 0.1, think);
  return true;
}
qa_q2bm_spawn_module qa_q2bm_boss3_stand_module(void) {
  qa_q2bm_spawn_module module = {NULL, callbacks_fn, spawn_fn};
  return module;
}
