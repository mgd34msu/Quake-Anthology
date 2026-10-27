/* Port of src/content/q1/missionpacks/world/rotate-targets.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/rotate_targets.h"
#include "../missionpacks_internal.h"
#include "qa/content/q1/missionpacks/world/common.h"
#include <math.h>
static double norm1(double angle) { return qa_q1mp_fround(angle - floor(angle / 360) * 360); }
qa_vec3 qa_q1mp_normalize_angles(qa_vec3 a) { return QA_Q1MP_V3(norm1(a.x), norm1(a.y), norm1(a.z)); }
/* TS vectors() = foundation angle vectors (landed core/math rule q1 via q1b make_vectors is game-bound; TS here uses pure vectors()). */
static qa_q1b_basis pure_vectors(qa_vec3 angles) { return qa_math_angle_vectors_rule(angles, QA_ANGLE_RULE_Q1); }
void qa_q1mp_link_rotate_targets(qa_q1mp_game *game, qa_q1mp_actor *entity) {
  qa_vec3 origin = game->base->body(game->base, entity).origin;
  qa_q1mp_world_vector(entity, "oldorigin", origin);
  qa_q1mp_actor *targets[64]; size_t n = game->base->find(game->base, entity->target, targets, 64);
  for (size_t i = 0; i < n; i++) {
    qa_q1mp_actor *target = targets[i]; qa_q1b_body body = game->base->body(game->base, target);
    bool wall = qa_q1mp_streq(target->classname, "func_movewall");
    qa_vec3 center = wall ? qa_q1mp_vadd(body.origin, qa_q1mp_vscale(qa_q1mp_vadd(body.bounds.min, body.bounds.max), 0.5)) : body.origin;
    qa_vec3 relative = qa_q1mp_vsub(center, origin);
    qa_q1mp_world_vector(target, "oldorigin", relative); qa_q1mp_world_vector(target, "neworigin", relative);
    qa_q1mp_world_number(target, "rotate_type", wall ? 1 : qa_q1mp_streq(target->classname, "rotate_object") ? 0 : 2);
    if (wall || qa_q1mp_streq(target->classname, "rotate_object")) target->owner = QA_Q1MP_SOME(entity->actor.id);
  }
}
void qa_q1mp_rotate_targets(qa_q1mp_game *game, qa_q1mp_actor *entity) {
  qa_q1b_body body = game->base->body(game->base, entity); qa_q1b_basis basis = pure_vectors(body.angles);
  qa_q1mp_actor *targets[64]; size_t n = game->base->find(game->base, entity->target, targets, 64);
  for (size_t i = 0; i < n; i++) {
    qa_q1mp_actor *target = targets[i]; qa_vec3 old = qa_q1mp_vector_of(target, "oldorigin"); double type = qa_q1mp_number(target, "rotate_type");
    qa_vec3 next = qa_q1mp_vadd(qa_q1mp_vadd(qa_q1mp_vscale(basis.forward, old.x), qa_q1mp_vscale(basis.right, -old.y)), qa_q1mp_vscale(basis.up, old.z));
    if (type == 1) {
      next = qa_q1mp_vadd(qa_q1mp_vsub(body.origin, qa_q1mp_vector_of(entity, "oldorigin")), qa_q1mp_vsub(next, old));
      qa_q1mp_world_vector(target, "neworigin", next);
      qa_q1b_body_patch p = {0}; p.has_velocity = true; p.velocity = qa_q1mp_vscale(qa_q1mp_vsub(next, game->base->body(game->base, target).origin), 25);
      game->base->set_body(game->base, target, &p);
    } else {
      qa_q1mp_world_vector(target, "neworigin", next);
      if (type == 0) { qa_q1b_body_patch p = {0}; p.has_angles = true; p.angles = body.angles; game->base->set_body(game->base, target, &p); }
      game->base->set_origin(game->base, target, qa_q1mp_vadd(next, body.origin));
    }
  }
}
void qa_q1mp_rotate_targets_final(qa_q1mp_game *game, qa_q1mp_actor *entity) {
  qa_q1mp_actor *targets[64]; size_t n = game->base->find(game->base, entity->target, targets, 64);
  for (size_t i = 0; i < n; i++) {
    qa_q1b_body_patch p = {0}; p.has_velocity = true; p.velocity = QA_Q1MP_ZERO;
    if (qa_q1mp_number(targets[i], "rotate_type") == 0) { p.has_angles = true; p.angles = game->base->body(game->base, entity).angles; }
    game->base->set_body(game->base, targets[i], &p);
  }
}
void qa_q1mp_set_target_origin(qa_q1mp_game *game, qa_q1mp_actor *entity) {
  qa_q1mp_actor *targets[64]; size_t n = game->base->find(game->base, entity->target, targets, 64);
  for (size_t i = 0; i < n; i++) {
    qa_q1mp_actor *t = targets[i];
    qa_vec3 o = qa_q1mp_number(t, "rotate_type") == 1
      ? qa_q1mp_vadd(qa_q1mp_vsub(game->base->body(game->base, entity).origin, qa_q1mp_vector_of(entity, "oldorigin")), qa_q1mp_vsub(qa_q1mp_vector_of(t, "neworigin"), qa_q1mp_vector_of(t, "oldorigin")))
      : qa_q1mp_vadd(qa_q1mp_vector_of(t, "neworigin"), game->base->body(game->base, entity).origin);
    game->base->set_origin(game->base, t, o);
  }
}
void qa_q1mp_damage_on_targets(qa_q1mp_game *game, qa_q1mp_actor *entity, double damage) {
  qa_q1mp_actor *targets[64]; size_t n = game->base->find(game->base, entity->target, targets, 64);
  for (size_t i = 0; i < n; i++)
    if (qa_q1mp_streq(targets[i]->classname, "trigger_hurt") || qa_q1mp_streq(targets[i]->classname, "func_movewall")) targets[i]->damage = damage;
}
