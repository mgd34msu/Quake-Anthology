/* Shared private helpers for the Q2 missionpacks slice (non-monsters),
 * over the one Q2 game (foundation/host.h). GPL-2.0-or-later. */
#ifndef QA_Q2MP_INTERNAL_H
#define QA_Q2MP_INTERNAL_H
#include "qa/foundation/js.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q2/foundation/monsters/types.h"
#include "qa/content/q2/foundation/scenery.h"
#include "qa/content/q2/foundation/weapons/ballistics.h"
#include "qa/core/math.h"

static inline qa_vec3 q2mp_v3(double x, double y, double z) { qa_vec3 v = {x, y, z}; return v; }
static inline qa_vec3 q2mp_zero(void) { return q2mp_v3(0, 0, 0); }
static inline double q2mp_now(qa_q2_game_services *g) { return g->host->now(g->host->ctx); }
static inline double q2mp_rnd(qa_q2_game_services *g) { return g->host->random(g->host->ctx); }
static inline double q2mp_frame(qa_q2_game_services *g) { return g->host->frame_seconds(g->host->ctx); }
static inline bool q2mp_rr(qa_q2_game_services *g) { return g->options.edition == QA_Q2_EDITION_RERELEASE; }
static inline bool q2mp_dm(qa_q2_game_services *g) { return g->options.mode == QA_Q2_MODE_DEATHMATCH; }
static inline bool q2mp_coop(qa_q2_game_services *g) { return g->options.mode == QA_Q2_MODE_COOP; }
static inline qa_body_state q2mp_body(qa_q2_game_services *g, qa_q2_entity *e) { return g->body(g->ctx, e->actor); }
static inline bool q2mp_live(qa_q2_game_services *g, qa_actor_id a) { return qa_session_actor_registry_is_live(g->host->actors, a); }
static inline qa_q2_entity *q2mp_ent(qa_q2_game_services *g, bool has, qa_actor_id a) {
  return has ? g->entity(g->ctx, &a) : g->entity(g->ctx, NULL);
}
static inline qa_body_state_result q2mp_bread(qa_q2_game_services *g, qa_actor_id a) {
  return qa_shared_body_table_read(g->host->bodies, a);
}
static inline qa_combat_state_result q2mp_cread(qa_q2_game_services *g, qa_actor_id a) {
  return qa_gameplay_authority_read(g->host->combat, a);
}
static inline double q2mp_health(qa_q2_game_services *g, qa_actor_id a) {
  qa_combat_state_result r = q2mp_cread(g, a);
  return r.has_value ? r.value.health : 0;
}
static inline bool q2mp_can_hurt(qa_q2_game_services *g, qa_actor_id a) {
  qa_combat_state_result r = q2mp_cread(g, a);
  return r.has_value && r.value.can_take_damage;
}
static inline qa_trace_result q2mp_trace(qa_q2_game_services *g, qa_vec3 start, qa_vec3 end, const qa_bounds *bounds,
                                         const qa_actor_id *ignore, double mask) {
  qa_q2_trace_request r;
  memset(&r, 0, sizeof r);
  r.start = start; r.end = end; r.bounds = bounds; r.ignore = ignore; r.mask = mask;
  return g->host->trace(g->host->ctx, r);
}
static inline qa_vec3 q2mp_normal(qa_trace_result t) { return t.contact.has_plane ? t.contact.plane.normal : q2mp_zero(); }
static inline qa_vec3 q2mp_cnormal(const qa_touch_contact *c) { return c->plane ? c->plane->normal : q2mp_zero(); }
static inline double q2mp_surf(const qa_touch_contact *c) { return c->surface ? c->surface->native_flags : 0; }
static inline void q2mp_move(qa_q2_game_services *g, qa_q2_entity *e, qa_q2_body_changes c, bool has_link, bool link) {
  g->move(g->ctx, e, c, has_link ? link : true); /* TS link = true */
}
static inline qa_q2_body_changes q2mp_changes(void) { qa_q2_body_changes c; memset(&c, 0, sizeof c); return c; }
static inline void q2mp_sound(qa_q2_game_services *g, qa_q2_entity *e, const char *path, bool hc, double ch,
                              bool hv, double vol, bool ha, double att) {
  /* TS defaults: channel 2, volume 1, attenuation 1. */
  g->sound(g->ctx, e->actor, path, hc ? ch : 2, hv ? vol : 1, ha ? att : 1);
}
static inline void q2mp_damage(qa_q2_game_services *g, qa_actor_id target, qa_q2_entity *inf_ent, qa_actor_id inf_actor,
                               const qa_actor_id *attacker, double amount, double kick, qa_vec3 dir, qa_vec3 point,
                               qa_vec3 normal, double mod, bool has_flags, double flags, const char *weapon) {
  (void)g->damage(g->ctx, target, inf_ent, inf_actor, attacker, amount, kick, dir, point, normal, mod,
                  has_flags ? flags : 0, weapon); /* TS flags = 0 */
}
static inline void q2mp_radius(qa_q2_game_services *g, qa_q2_entity *inf, const qa_actor_id *attacker, double damage,
                               const qa_actor_id *ignore, double radius, double mod, bool has_flags, double flags,
                               const char *weapon) {
  g->radius_damage(g->ctx, inf, attacker, damage, ignore, radius, mod, has_flags ? flags : 0, weapon);
}
static inline void q2mp_combat_create(qa_q2_game_services *g, qa_owned_actor actor, double health, double mass, bool can_take) {
  qa_combat_state s;
  memset(&s, 0, sizeof s);
  s.health = health; s.mass = mass; s.can_take_damage = can_take; s.invulnerable = false; s.team = NULL;
  s.armor.regular.kind = QA_REGULAR_ARMOR_NONE; s.armor.powered.kind = "none";
  qa_gameplay_authority_create_state(g->host->combat, actor, s, NULL, NULL);
}
static inline void q2mp_traits_hurt(qa_q2_game_services *g, qa_owned_actor actor, bool can_take) {
  qa_combat_trait_changes t;
  memset(&t, 0, sizeof t);
  t.has_can_take_damage = true; t.can_take_damage = can_take;
  qa_gameplay_authority_set_traits(g->host->combat, actor, t);
}
static inline void q2mp_traits_mass(qa_q2_game_services *g, qa_owned_actor actor, double mass) {
  qa_combat_trait_changes t;
  memset(&t, 0, sizeof t);
  t.has_mass = true; t.mass = mass;
  qa_gameplay_authority_set_traits(g->host->combat, actor, t);
}
/* ToUint32 (low 32 bits of ToInt32): NaN/Infinity give 0, values wrap modulo 2^32. */
static inline uint32_t q2mp_u32(double v) { return qa_js_to_uint32(v); }
/* JS `v | bits`: ToInt32 of both operands, signed int32 result. */
static inline double q2mp_js_or(double v, uint32_t bits) { return (double)(int32_t)(q2mp_u32(v) | bits); }
/* TS game.host.weaponBehavior?.controlsTrajectory(actor) === true. */
static inline bool q2mp_controls_trajectory(qa_q2_game_services *g, qa_actor_id a) {
  return g->host->weapon_behavior != NULL && g->host->weapon_behavior->controls_trajectory != NULL &&
         g->host->weapon_behavior->controls_trajectory(g->host->weapon_behavior->ctx, a);
}
/* TS game.host.actors.observations().map(o => o.id), copied: callers damage
 * actors while iterating, and a host may rebuild its array on removal. */
static inline qa_actor_id *q2mp_observations(qa_q2_game_services *g, size_t *out_count) {
  size_t n = 0;
  const qa_actor_observation *seen = qa_session_actor_registry_observations(g->host->actors, &n);
  qa_actor_id *copy = malloc((n ? n : 1) * sizeof *copy);
  if (copy == NULL) qa_raise("Error", 0, 0, "Out of memory reading actor observations");
  for (size_t i = 0; i < n; i++) copy[i] = seen[i].id;
  *out_count = n;
  return copy;
}
/* TS [...game.entities.values()]: a snapshot the loop body may remove from. */
static inline qa_q2_entity **q2mp_entity_snapshot(qa_q2_game_services *g, size_t *out_count) {
  size_t n = 0;
  qa_q2_entity *const *entities = g->entities(g->ctx, &n);
  qa_q2_entity **copy = malloc((n ? n : 1) * sizeof *copy);
  if (copy == NULL) qa_raise("Error", 0, 0, "Out of memory copying Q2 entities");
  if (n > 0) memcpy(copy, entities, n * sizeof *copy);
  *out_count = n;
  return copy;
}
/* False once a snapshot entry was removed earlier in the same loop: TS keeps
 * iterating the dead object, whose actor checks then fail; C must not touch
 * the freed entity. */
static inline bool q2mp_entity_present(qa_q2_game_services *g, const qa_q2_entity *e) {
  size_t n = 0;
  qa_q2_entity *const *entities = g->entities(g->ctx, &n);
  for (size_t i = 0; i < n; i++) if (entities[i] == e) return true;
  return false;
}
static inline bool q2mp_touch_eq(qa_q2_touch a, qa_q2_touch b) { return a.fn == b.fn && a.ctx == b.ctx; }
static inline bool q2mp_use_eq(qa_q2_use a, qa_q2_use b) { return a.fn == b.fn && a.ctx == b.ctx; }
static inline bool q2mp_die_eq(qa_q2_die a, qa_q2_die b) { return a.fn == b.fn && a.ctx == b.ctx; }
static inline bool q2mp_think_is(qa_q2_think a, qa_q2_think b) { return a.fn == b.fn && a.ctx == b.ctx; }
static inline qa_angle_vectors q2mp_axes(qa_vec3 a) { return qa_math_angle_vectors_rule(a, QA_ANGLE_RULE_NATIVE); }
static inline qa_vec3 q2mp_vta(qa_vec3 v) { return qa_vector_to_angles_with_rule(v, QA_VECTOR_TO_ANGLES_NATIVE); }
#define q2mp_add qa_add3_double
#define q2mp_sub qa_sub3_double
#define q2mp_scale qa_scale3_double
#define q2mp_dot qa_dot3_double
#define q2mp_len qa_hypot3
#define q2mp_norm qa_normalize3_double_hypot
#endif
