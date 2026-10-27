/* Port of src/content/q1/missionpacks/monsters/helpers.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/helpers.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "monsters_internal.h"

const qa_bounds qa_q1mpm_human_bounds = {{-16, -16, -24}, {16, 16, 40}};
const qa_bounds qa_q1mpm_hull_bounds = {{-16, -16, -24}, {16, 16, 32}};

void qa_q1mpm_number(qa_q1mpm_mission_monster *monster, const char *key, double value) {
  char buf[32];
  qa_q1mpm_format_fround(buf, sizeof(buf), value);
  qa_q1mpm_fields_set(monster->entity, key, buf);
}
bool qa_q1mpm_drop_to_floor(qa_q1mpm_mission_monster *monster) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  qa_q1mpm_body body = game->body(game, entity);
  qa_q1mpm_trace_request request = {body.origin, qa_q1mpm_vadd(body.origin, QA_Q1MPM_V3(0, 0, -256)),
                                    body.bounds, QA_Q1MPM_SOME(entity->actor.id), false, false};
  qa_q1mpm_trace trace = game->host->trace(game->host->ctx, &request);
  if (trace.fraction == 1 || trace.all_solid) return false;
  entity->movement_flags |= 512;
  qa_q1mpm_body_patch patch = {0};
  patch.has_origin = true; patch.origin = trace.end;
  patch.has_ground = true; patch.ground = trace.actor;
  game->set_body(game, entity, &patch);
  game->link(game, entity);
  return true;
}
void qa_q1mpm_gib(qa_q1mpm_mission_monster *monster, const char *head,
                  const char *const *gibs, size_t gib_count, const char *sound) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  const char *sound_path = sound == NULL ? "player/udeath.wav" : sound;
  if (sound_path[0] != '\0') qa_q1mpm_game_sound(game, entity, sound_path);
  qa_q1mpm_ext_throw_head(game, entity, head);
  for (size_t i = 0; i < gib_count; i++)
    qa_q1mpm_ext_throw_gib(game, qa_q1mpm_monster_origin(monster), gibs[i], game->health(game, entity->actor.id));
}
bool qa_q1mpm_eye(qa_q1mpm_game *game, qa_actor_id actor, qa_vec3 *out) {
  qa_q1mpm_body body;
  if (!game->host->bodies_read(game->host->ctx, actor, &body)) return false;
  qa_q1mpm_actor *entity = game->entity(game, QA_Q1MPM_SOME(actor));
  qa_vec3 offset = entity != NULL && qa_q1mpm_fields_has(entity, "view_ofs")
                       ? qa_q1mpm_actor_vector(entity, "view_ofs")
                       : QA_Q1MPM_V3(0, 0, game->is_player(game, QA_Q1MPM_SOME(actor)) ? 22
                                           : ((entity == NULL ? 0 : entity->movement_flags) & 2) != 0 ? 10 : 25);
  *out = qa_q1mpm_vadd(body.origin, offset);
  return true;
}
size_t qa_q1mpm_radius_actors(qa_q1mpm_game *game, qa_vec3 origin, double radius,
                              qa_actor_id *out, size_t capacity) {
  size_t observed_capacity = 1024;
  qa_owned_actor *observed = malloc(observed_capacity * sizeof(*observed));
  if (observed == NULL) return 0;
  size_t observed_count = game->host->actors_observations(game->host->ctx, observed, observed_capacity);
  size_t written = 0;
  /* TS flatMap(...).reverse(): scan observations backwards. */
  for (size_t i = observed_count; i-- > 0;) {
    qa_actor_id id = observed[i].id;
    qa_q1mpm_actor *entity = game->entity(game, QA_Q1MPM_SOME(id));
    qa_q1mpm_body body;
    if (!game->host->bodies_read(game->host->ctx, id, &body)) continue;
    if (entity != NULL && qa_q1mpm_str_eq(entity->solid, "none")) continue;
    qa_vec3 center = qa_q1mpm_vadd(body.origin, qa_q1mpm_vscale(qa_q1mpm_vadd(body.bounds.min, body.bounds.max), 0.5));
    if (qa_q1mpm_length(qa_q1mpm_vsub(origin, center)) <= radius && written < capacity) out[written++] = id;
  }
  free(observed);
  return written;
}
void qa_q1mpm_eel_zap(qa_q1mpm_mission_monster *monster) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  qa_actor_id targets[256];
  size_t count = qa_q1mpm_radius_actors(game, qa_q1mpm_monster_origin(monster), 85, targets, 256);
  for (size_t i = 0; i < count; i++) {
    qa_actor_id target = targets[i];
    if (qa_q1mpm_str_eq(game->host->classname(game->host->ctx, target), "monster_eel")) continue;
    qa_q1mpm_actor *other = game->entity(game, QA_Q1MPM_SOME(target));
    if (((other == NULL ? 0 : other->movement_flags) & 16) == 0) continue;
    qa_q1mpm_body body;
    if (!game->host->bodies_read(game->host->ctx, target, &body)) continue;
    qa_q1mpm_combat_read combat;
    if (!game->host->combat_read(game->host->ctx, target, &combat) || !combat.can_take_damage) continue;
    qa_vec3 center = qa_q1mpm_vadd(body.origin, qa_q1mpm_vscale(qa_q1mpm_vadd(body.bounds.min, body.bounds.max), 0.5));
    double points = 45 - fmax(0, 0.5 * qa_q1mpm_length(qa_q1mpm_vsub(qa_q1mpm_monster_origin(monster), center)));
    if (qa_same_actor(target, entity->actor.id)) points *= 0.5;
    if (points > 0 && game->can_damage(game, target, entity->actor.id))
      qa_q1mpm_game_damage(game, target, entity->actor.id, QA_Q1MPM_SOME(entity->actor.id), points);
  }
}
qa_q1mpm_actor *qa_q1mpm_missile(qa_q1mpm_game *game, qa_actor_id owner, const char *classname,
                                 const char *model, qa_vec3 origin, qa_vec3 velocity,
                                 const char *touch, double seconds) {
  qa_q1mpm_actor *entity = game->create(game, classname);
  entity->owner = QA_Q1MPM_SOME(owner);
  snprintf(entity->model, sizeof(entity->model), "%s", model);
  entity->movement = "flymissile";
  entity->solid = "bbox";
  qa_q1mpm_body_patch patch = {0};
  patch.has_origin = true; patch.origin = origin;
  patch.has_velocity = true; patch.velocity = velocity;
  patch.has_bounds = true; patch.bounds = QA_Q1MPM_POINT;
  patch.has_ground = true; patch.ground = QA_Q1MPM_NONE;
  game->set_body(game, entity, &patch);
  entity->touch = game->named.touch(game->named.ctx, entity, touch);
  game->schedule(game, entity, seconds, game->named.action(game->named.ctx, entity, "SUB_Remove"));
  game->link(game, entity);
  return entity;
}
void qa_q1mpm_empty_pain(void) { }
void qa_q1mpm_set_point_bounds(qa_q1mpm_mission_monster *monster) {
  qa_bounds point = {QA_Q1MPM_ZERO, QA_Q1MPM_ZERO};
  monster->game->set_bounds(monster->game, monster->entity, point);
}
