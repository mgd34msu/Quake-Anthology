/* Port of src/content/q1/addons/ctf/observer.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/ctf/observer.h"

#include <float.h>
#include <math.h>
#include <string.h>

#include "qa/content/q1/addons/ctf/teams.h"

void qa_q1ad_ctf_become_observer(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  qa_q1ad_context *context = state->context;
  if (state->services.set_observer != NULL) state->services.set_observer(state->services.ctx, actor, true);
  qa_owned_actor owner = qa_q1ad_ctf_owner(state, actor);
  if (context->game.combat_set_health != NULL)
    context->game.combat_set_health(context->game.ctx, owner, 999);
  if (context->game.combat_set_can_take_damage != NULL)
    context->game.combat_set_can_take_damage(context->game.ctx, owner, false);
  qa_q1ad_body_patch patch = {.has_bounds = true, .bounds = {{-12, -12, -12}, {12, 12, 12}}};
  qa_q1ad_ctf_write_body(state, actor, &patch);
}
static void through_door(qa_q1ad_ctf_state *state, qa_actor_id actor, qa_q1ad_actor *door) {
  qa_q1ad_context *context = state->context;
  qa_q1ad_actor *master = door->door_group_count > 0 ? door->door_group[0]
      : door->has_owner ? qa_q1ad_entity(context, &door->owner) : NULL;
  if (master == NULL || master->state == NULL || strcmp(master->state, "bottom") != 0) return;
  qa_vec3 min = {DBL_MAX, DBL_MAX, DBL_MAX}, max = {-DBL_MAX, -DBL_MAX, -DBL_MAX};
  if (master->door_group_count == 0) {
    qa_q1ad_body body = qa_q1ad_body_of(context, master);
    min = qa_add3(body.origin, body.bounds.min);
    max = qa_add3(body.origin, body.bounds.max);
  } else
    for (size_t i = 0; i < master->door_group_count; i++) {
      qa_q1ad_body body = qa_q1ad_body_of(context, master->door_group[i]);
      qa_vec3 low = qa_add3(body.origin, body.bounds.min);
      qa_vec3 high = qa_add3(body.origin, body.bounds.max);
      if (low.x < min.x) min.x = low.x;
      if (low.y < min.y) min.y = low.y;
      if (low.z < min.z) min.z = low.z;
      if (high.x > max.x) max.x = high.x;
      if (high.y > max.y) max.y = high.y;
      if (high.z > max.z) max.z = high.z;
    }
  qa_q1ad_body body = qa_q1ad_ctf_body(state, actor);
  qa_vec3 low = qa_add3(body.origin, body.bounds.min);
  qa_vec3 high = qa_add3(body.origin, body.bounds.max);
  bool x = min.x + 15 < low.x && high.x < max.x - 15;
  bool y = min.y + 15 < low.y && high.y < max.y - 15;
  bool z = min.z + 15 < low.z && high.z < max.z - 15;
  qa_vec3 direction = {0, 0, 0}, origin = body.origin;
  if (x && y) {
    if (origin.z < min.z) { direction = (qa_vec3){0, 0, 1}; origin.z = max.z + 25; }
    else if (origin.z > max.z) { direction = (qa_vec3){0, 0, -1}; origin.z = min.z - 25; }
  } else if (x && z) {
    if (origin.y < min.y) { direction = (qa_vec3){0, 1, 0}; origin.y = max.y + 25; }
    else if (origin.y > max.y) { direction = (qa_vec3){0, -1, 0}; origin.y = min.y - 25; }
  } else if (y && z) {
    if (origin.x < min.x) { direction = (qa_vec3){1, 0, 0}; origin.x = max.x + 25; }
    else if (origin.x > max.x) { direction = (qa_vec3){-1, 0, 0}; origin.x = min.x - 25; }
  }
  if (qa_dot3(direction, qa_normalize3_or_zero(body.velocity)) >= 0.5) {
    qa_q1ad_body_patch patch = {.has_origin = true, .origin = origin};
    qa_q1ad_ctf_write_body(state, actor, &patch);
  }
}
static void through_teleporter(qa_q1ad_ctf_state *state, qa_actor_id actor, qa_q1ad_actor *teleporter) {
  qa_q1ad_context *context = state->context;
  qa_q1ad_body body = qa_q1ad_ctf_body(state, actor);
  qa_q1ad_body tele_body = qa_q1ad_body_of(context, teleporter);
  qa_vec3 direction = qa_sub3(qa_add3(tele_body.origin,
      qa_scale3(qa_add3(tele_body.bounds.min, tele_body.bounds.max), 0.5)), body.origin);
  /* The QC calls normalize without assigning its return value here. */
  if (qa_dot3(direction, body.velocity) <= 0.1) return;
  qa_q1ad_actor *found[4];
  size_t count = qa_q1ad_find(context, teleporter->target != NULL ? teleporter->target : "", found, 4);
  if (count == 0) return;
  qa_q1ad_actor *target = found[0];
  qa_angle_vectors basis = qa_math_angle_vectors_rule(target->mangle, QA_ANGLE_RULE_Q1);
  if (state->services.teleport != NULL)
    state->services.teleport(state->services.ctx, actor, qa_q1ad_body_of(context, target).origin,
        target->mangle, qa_scale3(basis.forward, 300), context->game.time + 0.7);
}
void qa_q1ad_ctf_observer_frame(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  qa_q1ad_context *context = state->context;
  qa_q1ad_body body = qa_q1ad_ctf_body(state, actor);
  qa_q1ad_ctf_input input = state->services.input != NULL
      ? state->services.input(state->services.ctx, actor) : (qa_q1ad_ctf_input){0};
  qa_angle_vectors basis = qa_math_angle_vectors_rule(input.view_angles, QA_ANGLE_RULE_Q1);
  qa_vec3 forward = basis.forward;
  qa_vec3 horizontal = {forward.x, forward.y, 0};
  double cosine = qa_length3(horizontal);
  double inverse = cosine == 0 ? 0 : 1 / cosine;
  qa_vec3 facing = qa_scale3(horizontal, inverse);
  qa_vec3 velocity = body.velocity;
  velocity.z = 0;
  qa_vec3 parallel = qa_scale3(facing, qa_dot3(facing, velocity));
  qa_vec3 strafe = qa_sub3(velocity, parallel);
  double along = qa_dot3(facing, velocity) < 0 ? -qa_length3(parallel) : qa_length3(parallel);
  qa_vec3 projected = qa_scale3(forward, along * inverse);
  projected.z += body.velocity.z * 0.75;
  double speed = qa_length3(projected);
  double maximum = 320 - 100 * forward.z;
  if (speed > maximum) projected = qa_scale3(projected, maximum / speed);
  if (fabs(body.angles.x) == 30) projected.z = -projected.z;
  qa_q1ad_body_patch velocity_patch = {.has_velocity = true, .velocity = qa_add3(projected, strafe)};
  qa_q1ad_ctf_write_body(state, actor, &velocity_patch);
  qa_q1ad_actor *entities[512];
  size_t count = qa_q1ad_entities(context, entities, 512);
  for (size_t i = 0; i < count; i++) {
    qa_q1ad_body target = qa_q1ad_body_of(context, entities[i]);
    qa_vec3 center = qa_add3(target.origin, qa_scale3(qa_add3(target.bounds.min, target.bounds.max), 0.5));
    if (qa_length3(qa_sub3(center, body.origin)) > 75) continue;
    const char *classname = entities[i]->classname != NULL ? entities[i]->classname : "";
    if (strcmp(classname, "func_door") == 0) { through_door(state, actor, entities[i]); break; }
    if (strcmp(classname, "trigger_teleport") == 0) { through_teleporter(state, actor, entities[i]); break; }
  }
  if (input.jump && qa_q1ad_ctf_number(state, actor, "observerJumpHeld") == 0) {
    qa_q1ad_actor *spot = qa_q1ad_ctf_spawn_point(state, actor);
    if (spot != NULL && state->services.teleport != NULL) {
      qa_q1ad_body spot_body = qa_q1ad_body_of(context, spot);
      state->services.teleport(state->services.ctx, actor,
          qa_add3(spot_body.origin, (qa_vec3){0, 0, 1}), spot_body.angles,
          qa_q1ad_ctf_body(state, actor).velocity, input.teleport_until);
    }
  }
  qa_q1ad_ctf_set(state, actor, "observerJumpHeld", input.jump ? 1 : 0);
}
