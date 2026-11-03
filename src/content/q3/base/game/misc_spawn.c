/* Port of src/content/q3/base/game/misc-spawn.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/misc_spawn.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#include "qa/foundation/js.h"
static float f32(double v) { return (float)v; }
static qa_q3_entity_callback cb_use(qa_q3_entity_use_fn fn, void *ctx) { qa_q3_entity_callback cb; memcpy(&cb.fn, &fn, sizeof fn); cb.ctx = ctx; return cb; }
static qa_q3_entity_callback cb_think(qa_q3_entity_think_fn fn, void *ctx) { qa_q3_entity_callback cb; memcpy(&cb.fn, &fn, sizeof fn); cb.ctx = ctx; return cb; }
static void owned(qa_q3_misc_spawn_runtime *r, qa_q3_game_entity *e) {
  if (qa_q3_entity_pool_get(r->host.pool, e->slot) != e)
    qa_range_error("Misc spawn entity does not belong to its entity pool or was replaced");
}
static qa_q3_portal_context portal_ctx(qa_q3_misc_spawn_runtime *r) {
  qa_q3_portal_context c; memset(&c, 0, sizeof c);
  c.selection.pool = r->host.pool; c.selection.ctx = r->host.ctx;
  c.selection.random_int = r->host.rand_value; c.selection.warn = r->host.warn;
  c.world = r->host.world; c.time = r->host.time(r->host.ctx); return c;
}
static double crandom_checked(qa_q3_misc_spawn_runtime *r) {
  double v = r->host.crandom(r->host.ctx);
  if (!isfinite(v) || v < -1 || v > 1) qa_range_error("Shooter crandom() must return a value within [-1, 1]");
  return f32(v);
}
static void use_shooter(void *ctx, qa_q3_game_entity *e, const qa_q3_use_participant *o, const qa_q3_use_participant *a) {
  (void)o; (void)a; qa_q3_misc_spawn_runtime *r = ctx; owned(r, e);
  qa_vec3 initial = e->enemy ? qa_normalize3(qa_sub3(qa_q3_base_entity_shared_current_origin(&e->enemy->r), e->s.origin)) : e->movedir;
  qa_vec3 up = qa_perpendicular_vector(initial), right = qa_cross3(up, initial);
  double vertical = f32(crandom_checked(r) * f32(e->random));
  qa_vec3 with_v = qa_add3(initial, qa_scale3(up, vertical));
  double horizontal = f32(crandom_checked(r) * f32(e->random));
  qa_vec3 d = qa_normalize3(qa_add3(with_v, qa_scale3(right, horizontal)));
  qa_q3_missile_direction dir = {d.x, d.y, d.z};
  if (qa_js_to_int32(e->s.weapon) == QA_Q3_WP_GRENADE_LAUNCHER) r->host.fire_grenade(r->host.ctx, e, e->s.origin, dir);
  else if (qa_js_to_int32(e->s.weapon) == QA_Q3_WP_ROCKET_LAUNCHER) r->host.fire_rocket(r->host.ctx, e, e->s.origin, dir);
  else if (qa_js_to_int32(e->s.weapon) == QA_Q3_WP_PLASMAGUN) r->host.fire_plasma(r->host.ctx, e, e->s.origin, dir);
  qa_q3_entity_pool_add_event(r->host.pool, e, QA_Q3_EV_FIRE_WEAPON, 0);
}
static void finish_shooter(void *ctx, qa_q3_game_entity *e) {
  qa_q3_misc_spawn_runtime *r = ctx;
  qa_q3_target_selection_context sel; memset(&sel, 0, sizeof sel);
  sel.pool = r->host.pool; sel.ctx = r->host.ctx; sel.random_int = r->host.rand_value; sel.warn = r->host.warn;
  e->enemy = qa_q3_pick_target(sel, e->target);
  e->think.fn = NULL; e->think.ctx = NULL; qa_q3_game_entity_set_nextthink(e, 0);
}
void qa_q3_misc_spawn_bind_save_callbacks(qa_q3_misc_spawn_runtime *r) {
  qa_q3_callback_family_intern(&r->host.pool->callbacks.use, "q3.base.game.misc-spawn.shooter.use", cb_use(use_shooter, r));
  qa_q3_callback_family_intern(&r->host.pool->callbacks.think, "q3.base.game.misc-spawn.shooter.think", cb_think(finish_shooter, r));
}
void qa_q3_misc_spawn_runtime_init(qa_q3_misc_spawn_runtime *r, qa_q3_misc_spawn_host host) {
  if (host.item_product != host.missile_product) qa_range_error("Misc spawn item registry does not match its missile product");
  r->host = host; qa_q3_misc_spawn_bind_save_callbacks(r); qa_q3_bind_portal_save_callbacks(portal_ctx(r));
}
static void shooter(qa_q3_misc_spawn_runtime *r, qa_q3_game_entity *e, double weapon) {
  owned(r, e);
  e->use = qa_q3_callback_family_resolve(&r->host.pool->callbacks.use, "q3.base.game.misc-spawn.shooter.use");
  e->s.weapon = weapon;
  r->host.register_item(r->host.ctx, qa_q3_base_find_item_for_weapon(r->host.item_product, (qa_q3_weapon)weapon));
  qa_q3_move_direction_result moved = qa_q3_move_direction(e->s.angles);
  e->movedir = moved.direction; e->s.angles = moved.angles;
  if (e->random == 0) e->random = 1;
  double radians = f32(f32(f32(3.141592653589793) * f32(e->random)) / f32(180));
  e->random = f32(sin(radians));
  if (e->target) {
    e->think = qa_q3_callback_family_resolve(&r->host.pool->callbacks.think, "q3.base.game.misc-spawn.shooter.think");
    qa_q3_game_entity_set_nextthink(e, qa_int32(r->host.time(r->host.ctx) + 500));
  }
  if (r->host.pool->options.link) r->host.pool->options.link(r->host.pool->options.ctx, e);
}
static void h_info_camp(void *ctx, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) { (void)v; owned(ctx, e); qa_q3_set_origin(e, e->s.origin); }
static void h_free(void *ctx, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) { (void)v; qa_q3_misc_spawn_runtime *r = ctx; owned(r, e); qa_q3_entity_pool_free(r->host.pool, e); }
static void h_noop(void *ctx, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) { (void)v; owned(ctx, e); }
static void h_surface(void *ctx, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) { (void)v; qa_q3_misc_spawn_runtime *r = ctx; owned(r, e); qa_q3_spawn_portal_surface(portal_ctx(r), e); }
static void h_camera(void *ctx, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) { qa_q3_misc_spawn_runtime *r = ctx; owned(r, e); qa_q3_spawn_portal_camera(r->host.world, e, qa_q3_spawn_variables_float(v, "roll", "0").value); }
static void h_rocket(void *ctx, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) { (void)v; shooter(ctx, e, QA_Q3_WP_ROCKET_LAUNCHER); }
static void h_plasma(void *ctx, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) { (void)v; shooter(ctx, e, QA_Q3_WP_PLASMAGUN); }
static void h_grenade(void *ctx, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) { (void)v; shooter(ctx, e, QA_Q3_WP_GRENADE_LAUNCHER); }
size_t qa_q3_misc_spawn_runtime_handlers(qa_q3_misc_spawn_runtime *r, qa_q3_spawn_handler_entry *out, size_t cap) {
  static const struct { const char *name; qa_q3_spawn_handler_fn fn; } T[] = {
    {"info_camp", h_info_camp}, {"info_null", h_free}, {"info_notnull", h_info_camp}, {"light", h_free},
    {"misc_teleporter_dest", h_noop}, {"misc_model", h_free}, {"misc_portal_surface", h_surface},
    {"misc_portal_camera", h_camera}, {"shooter_rocket", h_rocket}, {"shooter_plasma", h_plasma}, {"shooter_grenade", h_grenade}};
  size_t n = sizeof T / sizeof *T;
  for (size_t i = 0; i < n && i < cap; i++) { out[i].classname = T[i].name; out[i].fn = T[i].fn; out[i].ctx = r; }
  return n;
}
size_t qa_q3_misc_spawn_handlers(qa_q3_misc_spawn_host host, qa_q3_spawn_handler_entry *out, size_t cap) {
  qa_q3_misc_spawn_runtime *r = malloc(sizeof *r); if (!r) qa_range_error("out of memory");
  qa_q3_callback_catalog_retain(&host.pool->callbacks, r);
  qa_q3_misc_spawn_runtime_init(r, host); return qa_q3_misc_spawn_runtime_handlers(r, out, cap);
}
