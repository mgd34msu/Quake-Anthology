/* Port of src/content/q1/missionpacks/monsters/helpers.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/helpers.h"

#include <math.h>
#include <stdlib.h>

#include "monsters_internal.h"

const qa_bounds qa_q1mpm_human_bounds = {{-16, -16, -24}, {16, 16, 40}};
const qa_bounds qa_q1mpm_hull_bounds = {{-16, -16, -24}, {16, 16, 32}};

void qa_q1mpm_number(qa_q1mpm_mission_monster *monster, const char *key, double value) {
  q1mpm_set_fround_text(monster->base.entity, key, value);
}
bool qa_q1mpm_drop_to_floor(qa_q1mpm_mission_monster *monster) {
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_q1_body body = qa_q1_get_body(game, entity);
  qa_q1_trace trace = q1mpm_trace(game, body.origin, qa_q1_vadd(body.origin, (qa_vec3){0, 0, -256}), body.bounds,
                                  &entity->actor.id, false);
  if (trace.fraction == 1 || trace.all_solid) return false;
  entity->movement_flags = qa_js_bit_or(entity->movement_flags, 512);
  qa_q1_body_patch patch = {0};
  patch.has_origin = true; patch.origin = trace.end;
  patch.has_ground = true; patch.ground = trace.has_actor ? &trace.actor : NULL;
  qa_q1_set_body(game, entity, &patch);
  qa_q1_link(game, entity);
  return true;
}
void qa_q1mpm_gib(qa_q1mpm_mission_monster *monster, const char *head,
                  const char *const *gibs, size_t gib_count, const char *sound) {
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  const char *sound_path = sound == NULL ? "player/udeath.wav" : sound;
  if (sound_path[0] != '\0') qa_q1_sound_defaults(game, entity->actor.id, sound_path);
  qa_q1_throw_head(game, entity, head);
  for (size_t i = 0; i < gib_count; i++) {
    const qa_vec3 origin = qa_q1_base_monster_origin(&monster->base);
    (void)qa_q1_throw_gib(game, origin, gibs[i], qa_q1_health(game, entity->actor.id));
  }
}
bool qa_q1mpm_eye(qa_q1_services *game, qa_actor_id actor, qa_vec3 *out) {
  qa_q1_body body;
  const bool has_body = q1mpm_read_body(game, actor, &body);
  qa_q1_actor *entity = qa_q1_get_entity(game, &actor);
  if (!has_body) return false;
  qa_vec3 offset;
  if (entity != NULL && qa_q1_actor_field_get(entity, "view_ofs") != NULL) offset = qa_q1_actor_vector(entity, "view_ofs");
  else offset = (qa_vec3){0, 0, qa_q1_is_player(game, &actor) ? 22
                                 : qa_js_bit_and(entity == NULL ? 0 : entity->movement_flags, 2) != 0 ? 10 : 25};
  *out = qa_q1_vadd(body.origin, offset);
  return true;
}
qa_actor_id *qa_q1mpm_radius_actors(qa_q1_services *game, qa_vec3 origin, double radius, size_t *out_count) {
  size_t observed_count = 0;
  const qa_actor_observation *observed = qa_session_actor_registry_observations(game->host.actors, &observed_count);
  qa_actor_id *ids = malloc((observed_count == 0 ? 1 : observed_count) * sizeof *ids);
  if (ids == NULL) qa_range_error("out of memory");
  for (size_t i = 0; i < observed_count; i++) ids[i] = observed[i].id; /* the registry list is scratch */
  size_t written = 0;
  for (size_t i = 0; i < observed_count; i++) {
    const qa_actor_id id = ids[i];
    qa_q1_actor *entity = qa_q1_get_entity(game, &id);
    qa_q1_body body;
    if (!q1mpm_read_body(game, id, &body) || (entity != NULL && strcmp(entity->solid, "none") == 0)) continue;
    if (qa_q1_length(qa_q1_vsub(origin, qa_q1_vadd(body.origin, qa_q1_vscale(qa_q1_vadd(body.bounds.min, body.bounds.max), 0.5)))) <= radius)
      ids[written++] = id;
  }
  /* TS .reverse(). */
  for (size_t i = 0; i < written / 2; i++) {
    const qa_actor_id swap = ids[i];
    ids[i] = ids[written - 1 - i];
    ids[written - 1 - i] = swap;
  }
  *out_count = written;
  return ids;
}
void qa_q1mpm_eel_zap(qa_q1mpm_mission_monster *monster) {
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  size_t count = 0;
  qa_actor_id *targets = qa_q1mpm_radius_actors(game, qa_q1_base_monster_origin(&monster->base), 85, &count);
  for (size_t i = 0; i < count; i++) {
    const qa_actor_id target = targets[i];
    const char *classname = game->host.classname(game->host.ctx, target);
    qa_q1_actor *other = qa_q1_get_entity(game, &target);
    if ((classname != NULL && strcmp(classname, "monster_eel") == 0) ||
        qa_js_bit_and(other == NULL ? 0 : other->movement_flags, 16) == 0)
      continue;
    qa_q1_body body;
    if (!q1mpm_read_body(game, target, &body) || !q1mpm_can_take_damage(game, target)) continue;
    double points = 45 - qa_js_max(0, 0.5 * qa_q1_length(qa_q1_vsub(qa_q1_base_monster_origin(&monster->base),
        qa_q1_vadd(body.origin, qa_q1_vscale(qa_q1_vadd(body.bounds.min, body.bounds.max), 0.5)))));
    if (qa_same_actor(target, entity->actor.id)) points *= 0.5;
    if (points > 0 && qa_q1_can_damage(game, target, entity->actor.id))
      (void)qa_q1_damage(game, target, entity->actor.id, &entity->actor.id, points, NULL, NULL, NULL, NULL);
  }
  free(targets);
}
qa_q1_actor *qa_q1mpm_missile(qa_q1_services *game, qa_actor_id owner, const char *classname,
                              const char *model, qa_vec3 origin, qa_vec3 velocity,
                              const char *touch, double seconds) {
  qa_q1_actor *entity = qa_q1_create(game, classname, NULL, NULL);
  entity->has_owner = true;
  entity->owner = owner;
  q1mpm_set_text(&entity->model, model);
  entity->movement = "flymissile";
  entity->solid = "bbox";
  qa_q1_body_patch patch = {0};
  patch.has_origin = true; patch.origin = origin;
  patch.has_velocity = true; patch.velocity = velocity;
  patch.has_bounds = true; patch.bounds = QA_Q1_POINT;
  patch.has_ground = true; patch.ground = NULL;
  qa_q1_set_body(game, entity, &patch);
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, touch);
  qa_q1_schedule(game, entity, seconds, qa_q1_callback_registry_action(game->named, entity, "SUB_Remove"));
  qa_q1_link(game, entity);
  return entity;
}
