/* Port of src/content/q1/addons/ctf/observer.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/ctf/observer.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/addons/ctf/teams.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"

void qa_q1ad_ctf_become_observer(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  qa_q1_services *game = state->context->game;
  state->services.set_observer(state->services.ctx, actor, true);
  qa_gameplay_authority_set_health(game->host.combat, qa_q1ad_ctf_owner(state, actor), 999);
  qa_gameplay_authority_set_traits(game->host.combat, qa_q1ad_ctf_owner(state, actor),
                                   (qa_combat_trait_changes){.has_can_take_damage = true, .can_take_damage = false});
  qa_q1_body_patch patch = {.has_bounds = true, .bounds = {{-12, -12, -12}, {12, 12, 12}}};
  qa_q1ad_ctf_write_body(state, actor, &patch);
}
static void through_door(qa_q1ad_ctf_state *state, qa_actor_id actor, qa_q1_actor *door) {
  qa_q1_services *game = state->context->game;
  qa_q1_actor *master = door->door_group_count > 0 ? door->door_group[0]
                                                   : qa_q1_get_entity(game, door->has_owner ? &door->owner : NULL);
  if (master == NULL || master->state == NULL || strcmp(master->state, "bottom") != 0) return;
  qa_q1_actor *const *members = master->door_group_count == 0 ? &master : master->door_group;
  size_t member_count = master->door_group_count == 0 ? 1 : master->door_group_count;
  qa_vec3 min = {INFINITY, INFINITY, INFINITY}, max = {-INFINITY, -INFINITY, -INFINITY};
  for (size_t i = 0; i < member_count; i++) {
    qa_q1_body body = qa_q1_get_body(game, members[i]);
    qa_vec3 low = qa_q1_vadd(body.origin, body.bounds.min), high = qa_q1_vadd(body.origin, body.bounds.max);
    min = (qa_vec3){qa_js_min(min.x, low.x), qa_js_min(min.y, low.y), qa_js_min(min.z, low.z)};
    max = (qa_vec3){qa_js_max(max.x, high.x), qa_js_max(max.y, high.y), qa_js_max(max.z, high.z)};
  }
  qa_q1_body body = qa_q1ad_ctf_body(state, actor);
  qa_vec3 low = qa_q1_vadd(body.origin, body.bounds.min), high = qa_q1_vadd(body.origin, body.bounds.max);
  bool x = min.x + 15 < low.x && high.x < max.x - 15, y = min.y + 15 < low.y && high.y < max.y - 15,
       z = min.z + 15 < low.z && high.z < max.z - 15;
  qa_vec3 direction = QA_Q1_ZERO, origin = body.origin;
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
  if (qa_q1_dot(direction, qa_q1_normalize(body.velocity)) >= 0.5) {
    qa_q1_body_patch patch = {.has_origin = true, .origin = origin};
    qa_q1ad_ctf_write_body(state, actor, &patch);
  }
}
static void through_teleporter(qa_q1ad_ctf_state *state, qa_actor_id actor, qa_q1_actor *teleporter) {
  qa_q1_services *game = state->context->game;
  qa_q1_body body = qa_q1ad_ctf_body(state, actor), tele = qa_q1_get_body(game, teleporter);
  qa_vec3 direction = qa_q1_vsub(qa_q1_vadd(tele.origin, qa_q1_vscale(qa_q1_vadd(tele.bounds.min, tele.bounds.max), 0.5)),
                                 body.origin);
  /* The QC calls normalize without assigning its return value here. */
  if (qa_q1_dot(direction, body.velocity) <= 0.1) return;
  qa_q1_actor *target = NULL;
  if (qa_q1_find(game, teleporter->target, &target, 1) == 0) return;
  state->services.teleport(state->services.ctx, actor, qa_q1_get_body(game, target).origin, target->mangle,
                           qa_q1_vscale(qa_q1_vectors(target->mangle).forward, 300), game->time + 0.7);
}
void qa_q1ad_ctf_observer_frame(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  qa_q1_services *game = state->context->game;
  qa_q1_body body = qa_q1ad_ctf_body(state, actor);
  qa_q1ad_ctf_input input = state->services.input(state->services.ctx, actor);
  qa_vec3 forward = qa_q1_vectors(input.view_angles).forward;
  qa_vec3 horizontal = {forward.x, forward.y, 0};
  double cosine = qa_q1_length(horizontal), inverse = cosine == 0 ? 0 : 1 / cosine;
  qa_vec3 facing = qa_q1_vscale(horizontal, inverse), velocity = {body.velocity.x, body.velocity.y, 0};
  qa_vec3 parallel = qa_q1_vscale(facing, qa_q1_dot(facing, velocity)), strafe = qa_q1_vsub(velocity, parallel);
  qa_vec3 projected = qa_q1_vscale(
      forward, (qa_q1_dot(facing, velocity) < 0 ? -qa_q1_length(parallel) : qa_q1_length(parallel)) * inverse);
  projected.z = projected.z + body.velocity.z * 0.75;
  double speed = qa_q1_length(projected), maximum = 320 - 100 * forward.z;
  if (speed > maximum) projected = qa_q1_vscale(projected, maximum / speed);
  if (fabs(body.angles.x) == 30) projected.z = -projected.z;
  qa_q1_body_patch patch = {.has_velocity = true, .velocity = qa_q1_vadd(projected, strafe)};
  qa_q1ad_ctf_write_body(state, actor, &patch);
  /* findradius skips SOLID_NOT and chains the highest edict first: [...entities].reverse(). */
  size_t count = game->entity_count;
  qa_q1_actor **entities = malloc((count != 0 ? count : 1) * sizeof *entities);
  if (entities == NULL) qa_range_error("out of memory");
  for (size_t i = 0; i < count; i++) entities[i] = game->entities[count - 1 - i];
  for (size_t i = 0; i < count; i++) {
    qa_q1_actor *entity = entities[i];
    if (strcmp(entity->solid, "none") == 0) continue;
    qa_q1_body target = qa_q1_get_body(game, entity);
    qa_vec3 center = qa_q1_vadd(target.origin, qa_q1_vscale(qa_q1_vadd(target.bounds.min, target.bounds.max), 0.5));
    if (qa_q1_length(qa_q1_vsub(center, body.origin)) > 75) continue;
    if (strcmp(entity->classname, "func_door") == 0) { through_door(state, actor, entity); break; }
    if (strcmp(entity->classname, "trigger_teleport") == 0) { through_teleporter(state, actor, entity); break; }
  }
  free(entities);
  if (input.jump && qa_q1ad_ctf_number(state, actor, "observerJumpHeld") == 0) {
    qa_q1_actor *spot = qa_q1ad_ctf_spawn_point(state, actor);
    if (spot != NULL) {
      qa_vec3 origin = qa_q1_vadd(qa_q1_get_body(game, spot).origin, (qa_vec3){0, 0, 1});
      qa_vec3 angles = qa_q1_get_body(game, spot).angles;
      qa_vec3 velocity = qa_q1ad_ctf_body(state, actor).velocity;
      state->services.teleport(state->services.ctx, actor, origin, angles, velocity, input.teleport_until);
    }
  }
  qa_q1ad_ctf_set(state, actor, "observerJumpHeld", input.jump ? 1 : 0);
}
