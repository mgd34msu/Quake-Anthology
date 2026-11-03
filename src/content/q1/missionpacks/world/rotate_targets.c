/* Port of src/content/q1/missionpacks/world/rotate-targets.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/rotate_targets.h"

#include "../missionpacks_internal.h"
#include "qa/content/q1/missionpacks/world/common.h"

static double normalize_angle(double angle) { return qa_q1mp_fround(angle - floor(angle / 360) * 360); }
qa_vec3 qa_q1mp_normalize_angles(qa_vec3 angles) {
  return QA_Q1MP_V3(normalize_angle(angles.x), normalize_angle(angles.y), normalize_angle(angles.z));
}
/* game.find(entity.target) as an owned array. */
static qa_q1_actor **targets_of(qa_q1_services *game, qa_q1_actor *entity, size_t *out_count) {
  const size_t count = qa_q1_find(game, entity->target, NULL, 0);
  qa_q1_actor **targets = malloc((count != 0 ? count : 1) * sizeof *targets);
  if (targets == NULL) qa_range_error("Out of memory");
  *out_count = qa_q1_find(game, entity->target, targets, count);
  return targets;
}
void qa_q1mp_link_rotate_targets(qa_q1_services *game, qa_q1_actor *entity) {
  const qa_vec3 origin = qa_q1_get_body(game, entity).origin;
  qa_q1mp_world_vector(entity, "oldorigin", origin);
  size_t count; qa_q1_actor **targets = targets_of(game, entity, &count);
  for (size_t i = 0; i < count; i++) {
    qa_q1_actor *target = targets[i];
    const qa_q1_body body = qa_q1_get_body(game, target);
    const bool wall = qa_q1mp_streq(target->classname, "func_movewall");
    const qa_vec3 center = wall ? qa_q1_vadd(body.origin, qa_q1_vscale(qa_q1_vadd(body.bounds.min, body.bounds.max), 0.5)) : body.origin;
    const qa_vec3 relative = qa_q1_vsub(center, origin);
    qa_q1mp_world_vector(target, "oldorigin", relative); qa_q1mp_world_vector(target, "neworigin", relative);
    qa_q1mp_world_number(target, "rotate_type", wall ? 1 : qa_q1mp_streq(target->classname, "rotate_object") ? 0 : 2);
    if (wall || qa_q1mp_streq(target->classname, "rotate_object")) { target->has_owner = true; target->owner = entity->actor.id; }
  }
  free(targets);
}
void qa_q1mp_rotate_targets(qa_q1_services *game, qa_q1_actor *entity) {
  const qa_q1_body body = qa_q1_get_body(game, entity);
  const qa_q1_basis basis = qa_q1_vectors(body.angles);
  size_t count; qa_q1_actor **targets = targets_of(game, entity, &count);
  for (size_t i = 0; i < count; i++) {
    qa_q1_actor *target = targets[i];
    const qa_vec3 old = qa_q1_actor_vector(target, "oldorigin");
    const double type = qa_q1_actor_number(target, "rotate_type");
    qa_vec3 next = qa_q1_vadd(qa_q1_vadd(qa_q1_vscale(basis.forward, old.x), qa_q1_vscale(basis.right, -old.y)), qa_q1_vscale(basis.up, old.z));
    if (type == 1) {
      next = qa_q1_vadd(qa_q1_vsub(body.origin, qa_q1_actor_vector(entity, "oldorigin")), qa_q1_vsub(next, old));
      qa_q1mp_world_vector(target, "neworigin", next);
      qa_q1_body_patch patch = {0};
      patch.has_velocity = true; patch.velocity = qa_q1_vscale(qa_q1_vsub(next, qa_q1_get_body(game, target).origin), 25);
      qa_q1_set_body(game, target, &patch);
    } else {
      qa_q1mp_world_vector(target, "neworigin", next);
      if (type == 0) { qa_q1_body_patch patch = {0}; patch.has_angles = true; patch.angles = body.angles; qa_q1_set_body(game, target, &patch); }
      qa_q1_set_origin(game, target, qa_q1_vadd(next, body.origin));
    }
  }
  free(targets);
}
void qa_q1mp_rotate_targets_final(qa_q1_services *game, qa_q1_actor *entity) {
  size_t count; qa_q1_actor **targets = targets_of(game, entity, &count);
  for (size_t i = 0; i < count; i++) {
    qa_q1_body_patch patch = {0};
    patch.has_velocity = true; patch.velocity = QA_Q1_ZERO;
    if (qa_q1_actor_number(targets[i], "rotate_type") == 0) { patch.has_angles = true; patch.angles = qa_q1_get_body(game, entity).angles; }
    qa_q1_set_body(game, targets[i], &patch);
  }
  free(targets);
}
void qa_q1mp_set_target_origin(qa_q1_services *game, qa_q1_actor *entity) {
  size_t count; qa_q1_actor **targets = targets_of(game, entity, &count);
  for (size_t i = 0; i < count; i++) {
    qa_vec3 a4;
    qa_vec3 b4;
    qa_vec3 a3;
    qa_vec3 b3;
    qa_vec3 a2;
    qa_vec3 b2;
    qa_vec3 a;
    qa_vec3 b;
    qa_q1_actor *target = targets[i];
    const qa_vec3 origin = qa_q1_actor_number(target, "rotate_type") == 1
      ? (a4 = (a3 = qa_q1_get_body(game, entity).origin, b3 = qa_q1_actor_vector(entity, "oldorigin"), qa_q1_vsub(a3, b3)), b4 = (a2 = qa_q1_actor_vector(target, "neworigin"), b2 = qa_q1_actor_vector(target, "oldorigin"), qa_q1_vsub(a2, b2)), qa_q1_vadd(a4,
                   b4))
      : (a = qa_q1_actor_vector(target, "neworigin"), b = qa_q1_get_body(game, entity).origin, qa_q1_vadd(a, b));
    qa_q1_set_origin(game, target, origin);
  }
  free(targets);
}
void qa_q1mp_damage_on_targets(qa_q1_services *game, qa_q1_actor *entity, double damage) {
  size_t count; qa_q1_actor **targets = targets_of(game, entity, &count);
  for (size_t i = 0; i < count; i++)
    if (qa_q1mp_streq(targets[i]->classname, "trigger_hurt") || qa_q1mp_streq(targets[i]->classname, "func_movewall")) targets[i]->damage = damage;
  free(targets);
}
