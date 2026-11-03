/* Port of src/content/q1/missionpacks/world/hipnotic-particles.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/hipnotic_particles.h"

#include "../missionpacks_internal.h"
#include "qa/content/q1/base/map_entities.h"
#include "qa/content/q1/missionpacks/world/common.h"

static bool falsy(double value) { return value == 0 || isnan(value); } /* JS `||=` on a number */
static void emit_particle(qa_q1_services *game, qa_q1_actor *entity, double x, double y, double z) {
  qa_q1_event event = {0};
  event.kind = "particles"; event.origin = QA_Q1MP_V3(x, y, z); event.direction = QA_Q1_ZERO;
  event.color = qa_q1_actor_number(entity, "color"); event.count = entity->count;
  qa_q1_emit(game, event);
}
static void field_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)ctx; (void)activator;
  qa_q1_actor *counter = qa_q1_get_entity(game, other);
  const double state = counter != NULL && qa_q1mp_streq(counter->classname, "func_counter") ? qa_q1_actor_number(counter, "counter_state") : 0;
  if (qa_js_bit_and(entity->spawnflags, 1) != 0 && state != qa_q1_actor_number(entity, "cnt")) return;
  qa_q1mp_world_number(entity, "ltime", game->time + 0.25);
  if (qa_q1_actor_text(entity, "noise")[0] != '\0') qa_q1_sound(game, entity->actor.id, qa_q1_actor_text(entity, "noise"), "voice", 1, 1);
  qa_actor_id client;
  if (!game->host.check_client(game->host.ctx, entity->actor, &client)) return;
  const qa_vec3 start = qa_q1_vadd(entity->dest1, qa_q1_get_body(game, entity).origin);
  const qa_vec3 end = qa_q1_vadd(entity->dest2, qa_q1_get_body(game, entity).origin);
  const double plane = qa_q1_actor_number(entity, "particle_plane");
  if (plane == 0) { for (double x = start.x; x <= end.x; x += 16) for (double z = start.z; z <= end.z; z += 16) emit_particle(game, entity, x, start.y, z); }
  else if (plane == 1) { for (double y = start.y; y < end.y; y += 16) for (double z = start.z; z < end.z; z += 16) emit_particle(game, entity, start.x, y, z); }
  else { for (double x = start.x; x < end.x; x += 16) for (double y = start.y; y < end.y; y += 16) emit_particle(game, entity, x, y, start.z); }
}
static void field_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                        const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  if (entity->damage == 0 || game->time > qa_q1_actor_number(entity, "ltime") || game->time < entity->attack_finished) return;
  entity->attack_finished = game->time + 0.5;
  (void)qa_q1_damage(game, other, entity->actor.id, &entity->actor.id, entity->damage, NULL, "direct", "", NULL);
}
static void particlefield_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  const qa_bounds bounds = qa_q1_get_body(game, entity).bounds;
  const qa_vec3 origin = qa_q1_vscale(qa_q1_vadd(bounds.min, bounds.max), 0.5);
  const qa_vec3 size = qa_q1_vsub(qa_q1_vsub(bounds.max, bounds.min), QA_Q1MP_V3(16, 16, 16));
  entity->dest1 = qa_q1_vsub(qa_q1_vadd(bounds.min, QA_Q1MP_V3(8, 8, 8)), origin);
  entity->dest2 = qa_q1_vsub(qa_q1_vadd(bounds.max, QA_Q1MP_V3(7.9, 7.9, 7.9)), origin);
  if (size.x > size.z && size.y > size.z) { qa_q1mp_world_number(entity, "particle_plane", 2); entity->dest1.z = (entity->dest1.z + entity->dest2.z) / 2; }
  else if (size.x <= size.z && size.y > size.x) { qa_q1mp_world_number(entity, "particle_plane", 1); entity->dest1.x = (entity->dest1.x + entity->dest2.x) / 2; }
  else { qa_q1mp_world_number(entity, "particle_plane", 0); entity->dest1.y = (entity->dest1.y + entity->dest2.y) / 2; }
  qa_q1mp_set_string(&entity->model, ""); entity->solid = "none"; entity->movement = "none";
  if (falsy(entity->count)) entity->count = 2;
  if (qa_q1_actor_number(entity, "color") == 0) qa_q1mp_world_number(entity, "color", 192);
  entity->use = qa_q1_callback_registry_use(game->named, entity, "hip:particlefield");
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, "hip:particlefield");
  qa_q1_set_origin(game, entity, origin);
}
static void togglewall_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)ctx; (void)other; (void)activator;
  const bool active = qa_q1_actor_number(entity, "toggle_state") == 0;
  qa_q1mp_world_number(entity, "toggle_state", active ? 1 : 0);
  const double displacement = active ? -8000 : 8000;
  qa_q1_set_origin(game, entity, qa_q1_vadd(qa_q1_get_body(game, entity).origin, QA_Q1MP_V3(displacement, displacement, displacement)));
  qa_q1_sound(game, entity->actor.id, qa_q1_actor_text(entity, active ? "noise1" : "noise"), "voice", 1, 1);
}
static void togglewall_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                             const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  if (entity->damage == 0 || game->time < entity->attack_finished) return;
  entity->attack_finished = game->time + 0.5;
  (void)qa_q1_damage(game, other, entity->actor.id, &entity->actor.id, entity->damage, NULL, "direct", "", NULL);
}
static void togglewall_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  entity->movement = "push"; entity->solid = "bsp"; qa_q1mp_set_string(&entity->model, "");
  entity->use = qa_q1_callback_registry_use(game->named, entity, "hip:togglewall");
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, "hip:togglewall");
  if (qa_q1_actor_text(entity, "noise")[0] == '\0') qa_q1mp_field_text(entity, "noise", "misc/null.wav");
  if (qa_q1_actor_text(entity, "noise1")[0] == '\0') qa_q1mp_field_text(entity, "noise1", "misc/null.wav");
  if (qa_js_bit_and(entity->spawnflags, 1) != 0) {
    qa_q1mp_world_number(entity, "toggle_state", 0);
    qa_q1_set_origin(game, entity, qa_q1_vadd(qa_q1_get_body(game, entity).origin, QA_Q1MP_V3(8000, 8000, 8000)));
    return;
  }
  qa_q1mp_world_number(entity, "toggle_state", 1);
  qa_q1_sound(game, entity->actor.id, qa_q1_actor_text(entity, "noise1"), "voice", 1, 1);
}
static void wallsprite_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  if (entity->model[0] == '\0') qa_q1mp_set_string(&entity->model, "progs/s_blood1.spr");
  (void)qa_q1_services_precache_model(game, entity->model);
  entity->solid = "none"; entity->movement = "none";
  qa_vec3 angles = qa_q1_get_body(game, entity).angles;
  if (angles.x == 0 && angles.y == -1 && angles.z == 0) angles = QA_Q1MP_V3(-90, 0, 0);
  else if (angles.x == 0 && angles.y == -2 && angles.z == 0) angles = QA_Q1MP_V3(90, 0, 0);
  qa_q1_body_patch patch = {0};
  patch.has_angles = true; patch.angles = angles;
  patch.has_origin = true; patch.origin = qa_q1_vsub(qa_q1_get_body(game, entity).origin, qa_q1_vscale(qa_q1_vectors(angles).forward, 0.2));
  qa_q1_set_body(game, entity, &patch);
  qa_q1_make_static(game, entity);
}
void qa_q1mp_register_hipnotic_particles(qa_q1_services *game) {
  qa_q1_callback_registry_register(game->named, "hip:particlefield", (qa_q1_callback_handlers){.use = field_use, .touch = field_touch});
  qa_q1_register_spawn(game, "func_particlefield", particlefield_spawn, NULL);
  qa_q1_callback_registry_register(game->named, "hip:togglewall", (qa_q1_callback_handlers){.use = togglewall_use, .touch = togglewall_touch});
  qa_q1_register_spawn(game, "func_togglewall", togglewall_spawn, NULL);
  qa_q1_register_spawn(game, "wallsprite", wallsprite_spawn, NULL);
}
