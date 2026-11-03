/* Port of src/content/q1/missionpacks/world/rogue-misc.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/rogue_misc.h"
#include "../missionpacks_internal.h"
#include "qa/content/q1/base/map_entities.h"
#include "qa/content/q1/missionpacks/world/common.h"

static void torch_spawn(void *ctx, qa_q1_services *source, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1mp_set_string(&entity->model, qa_q1_services_precache_model(source, "progs/flame.mdl"));
  if ((qa_js_to_int32(entity->spawnflags) & 1) == 0) {
    const char *path = qa_q1_services_precache_sound(source, "ambience/fire1.wav");
    qa_q1_event event = {0};
    event.kind = "ambient"; event.has_origin = true; event.origin = qa_q1_get_body(source, entity).origin;
    event.path = path; event.volume = 0.5; event.attenuation = 3;
    qa_q1_emit(source, event);
  }
  qa_q1_make_static(source, entity);
}
static void light_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  const char *path = ctx; /* "progs/lantern.mdl" | "progs/candle.mdl" */
  qa_q1mp_set_string(&entity->model, qa_q1_services_precache_model(game, path));
  entity->solid = "none"; entity->movement = "none";
  qa_q1_make_static(game, entity);
}
static void rubble_touch(void *ctx, qa_q1_services *g, qa_q1_actor *e, qa_actor_id other, const qa_vec3 *normal,
                         const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  qa_q1_actor *target = qa_q1_get_entity(g, &other);
  const bool hit = qa_q1_is_player(g, &other) || (qa_js_to_int32(target != NULL ? target->movement_flags : 0) & 32) != 0;
  if (hit && qa_q1_length(qa_q1_get_body(g, e).velocity) > 0) (void)qa_q1_damage(g, other, e->actor.id, &e->actor.id, 10, NULL, NULL, NULL, NULL);
}
static void rubble_throw(void *ctx, qa_q1_services *g, qa_q1_actor *e) {
  (void)ctx;
  qa_q1_actor *destination = qa_q1mp_find_first(g, e->target);
  if (destination == NULL) destination = g->world;
  if (destination == NULL) qa_range_error("Rubble generator requires worldspawn");
  qa_vec3 a = qa_q1_get_body(g, destination).origin;
  qa_vec3 b = qa_q1_get_body(g, e).origin;
  const qa_vec3 direction = qa_q1_normalize(qa_q1_vsub(a, b));
  qa_q1_actor *rubble = qa_q1_create(g, "rubble", NULL, NULL);
  rubble->has_owner = true; rubble->owner = e->actor.id; qa_q1mp_set_string(&rubble->model, "progs/rubble.mdl");
  rubble->solid = "bbox"; rubble->movement = "bounce"; rubble->skin = (qa_js_to_int32(e->spawnflags) & 1) != 0 ? 1 : 0;
  qa_q1_body_patch patch = {0};
  patch.has_origin = true; patch.origin = qa_q1_get_body(g, e).origin;
  qa_vec3 spread;
  spread.x = direction.x + g->host.random(g->host.ctx) * 0.2 - 0.1;
  spread.y = direction.y + g->host.random(g->host.ctx) * 0.2 - 0.1;
  spread.z = direction.z + g->host.random(g->host.ctx) * 0.2 - 0.1;
  patch.has_velocity = true; patch.velocity = qa_q1_vscale(spread, 300);
  patch.has_bounds = true; patch.bounds = (qa_bounds){{-16, -16, -16}, {16, 16, 16}};
  qa_q1_set_body(g, rubble, &patch);
  rubble->touch = qa_q1_callback_registry_touch(g->named, rubble, "rogue:rubble_touch");
  qa_q1mp_world_later(g, rubble, 30, "SUB_Remove"); qa_q1_link(g, rubble);
  qa_q1mp_world_later(g, e, e->delay, "rogue:rubble_throw");
}
static void rubble_use(void *ctx, qa_q1_services *g, qa_q1_actor *e, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)ctx; (void)other; (void)activator;
  if (e->wait == 0) { e->wait = 1; qa_q1mp_world_later(g, e, e->delay, "rogue:rubble_throw"); return; }
  e->wait = 0; qa_q1_cancel(g, e);
}
static void rubble_spawn(void *ctx, qa_q1_services *g, qa_q1_actor *e) {
  (void)ctx;
  if (e->target[0] == '\0') qa_range_error("rubble_generator has no target!");
  if (e->delay == 0 || isnan(e->delay)) e->delay = 5; /* TS ||= */
  e->solid = "none"; e->use = qa_q1_callback_registry_use(g->named, e, "rogue:rubble_use");
  if ((qa_js_to_int32(e->spawnflags) & 2) != 0) qa_q1_call_use(g, e, e->use, NULL, NULL);
}
static void explosion_die(void *ctx, qa_q1_services *g, qa_q1_actor *e, const qa_actor_id *attacker) {
  (void)ctx;
  qa_q1_use_targets(g, e, attacker); e->touch = (qa_q1_bound_touch){0};
  qa_q1mp_world_later(g, e, 0.1, "SUB_Remove");
}
static void explosion_spawn(void *ctx, qa_q1_services *g, qa_q1_actor *e) {
  (void)ctx;
  qa_q1mp_world_trigger(g, e);
  const double health = qa_q1_health(g, e->actor.id);
  e->max_health = health != 0 && !isnan(health) ? health : 20; /* TS || 20 */
  qa_gameplay_authority_set_health(g->host.combat, e->actor, e->max_health);
  e->die = qa_q1_callback_registry_die(g->named, e, "rogue:explosion_trigger_die");
  qa_q1_actor_set_damageable(e, true); e->solid = "bbox"; qa_q1_link(g, e);
}
static bool explosion_before_quad(void *ctx, qa_damage_request request, double damage, qa_combat_state target,
                                  const qa_combat_state *attacker, qa_damage_preparation *out) {
  (void)target; (void)attacker;
  qa_q1_services *game = ctx;
  qa_q1_actor *entity = qa_q1_get_entity(game, &request.target);
  const bool cancel = entity != NULL && qa_q1mp_streq(entity->classname, "trigger_explosion") &&
                      !qa_q1mp_streq(request.delivery, "radius");
  out->is_continue = !cancel; out->amount = cancel ? 0 : damage;
  return true;
}
void qa_q1mp_register_rogue_misc(qa_q1_services *game) {
  qa_q1_replace_spawn(game, "light_torch_small_walltorch", torch_spawn, NULL);
  qa_q1_register_spawn(game, "light_lantern", light_spawn, (void *)"progs/lantern.mdl");
  qa_q1_register_spawn(game, "light_candle", light_spawn, (void *)"progs/candle.mdl");
  qa_q1_callback_registry_register(game->named, "rogue:rubble_touch", (qa_q1_callback_handlers){.touch = rubble_touch});
  qa_q1_callback_registry_register(game->named, "rogue:rubble_throw", (qa_q1_callback_handlers){.action = rubble_throw});
  qa_q1_callback_registry_register(game->named, "rogue:rubble_use", (qa_q1_callback_handlers){.use = rubble_use});
  qa_q1_register_spawn(game, "rubble_generator", rubble_spawn, NULL);
  qa_q1_callback_registry_register(game->named, "rogue:explosion_trigger_die", (qa_q1_callback_handlers){.die = explosion_die});
  qa_q1_register_spawn(game, "trigger_explosion", explosion_spawn, NULL);
  qa_q1_damage_source_effects effects = {0};
  effects.ctx = game; effects.before_quad = explosion_before_quad;
  qa_q1_register_damage_source_effects(game, "rogue:explosion-trigger", effects);
}
