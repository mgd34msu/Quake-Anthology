/* Port of src/content/q1/missionpacks/world/common.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/common.h"
#include "../missionpacks_internal.h"
#include <stdio.h>
void qa_q1mp_world_number(qa_q1mp_actor *entity, const char *key, double value) {
  char buf[32]; qa_q1b_format_fround(buf, sizeof buf, value); qa_q1b_fields_set(entity, key, buf);
}
void qa_q1mp_world_vector(qa_q1mp_actor *entity, const char *key, qa_vec3 value) {
  char bx[32], by[32], bz[32], buf[128];
  /* TS template interpolates Math.fround(x) as JS numbers. */
  (void)snprintf(bx, sizeof bx, "%g", qa_q1mp_fround(value.x));
  (void)snprintf(by, sizeof by, "%g", qa_q1mp_fround(value.y));
  (void)snprintf(bz, sizeof bz, "%g", qa_q1mp_fround(value.z));
  (void)snprintf(buf, sizeof buf, "%s %s %s", bx, by, bz); qa_q1b_fields_set(entity, key, buf);
}
void qa_q1mp_world_later(qa_q1mp_game *game, qa_q1mp_actor *entity, double delay, const char *name) {
  game->base->schedule(game->base, entity, delay, game->base->named.action(game->base->named.ctx, entity, name));
}
static void zero_angles(qa_q1mp_game *game, qa_q1mp_actor *entity) {
  qa_q1b_body_patch p = {0}; p.has_angles = true; p.angles = QA_Q1MP_ZERO;
  game->base->set_body(game->base, entity, &p);
}
void qa_q1mp_world_trigger(qa_q1mp_game *game, qa_q1mp_actor *entity) {
  entity->movedir = qa_q1b_move_direction(game->base, game->base->body(game->base, entity).angles);
  entity->solid = "trigger"; entity->model[0] = '\0'; zero_angles(game, entity);
}
void qa_q1mp_world_brush(qa_q1mp_game *game, qa_q1mp_actor *entity) {
  entity->mangle = game->base->body(game->base, entity).angles;
  entity->solid = "bsp"; entity->movement = "push"; zero_angles(game, entity);
}
void qa_q1mp_world_target_event(qa_q1mp_game *game, qa_q1mp_actor *entity, const char *target, const char *message) {
  char prior[128], prior_message[512];
  (void)snprintf(prior, sizeof prior, "%s", entity->target);
  (void)snprintf(prior_message, sizeof prior_message, "%s", entity->message);
  (void)snprintf(entity->target, sizeof entity->target, "%s", target ? target : "");
  (void)snprintf(entity->message, sizeof entity->message, "%s", message ? message : "");
  game->base->use_targets(game->base, entity, entity->activator);
  (void)snprintf(entity->target, sizeof entity->target, "%s", prior);
  (void)snprintf(entity->message, sizeof entity->message, "%s", prior_message);
}
