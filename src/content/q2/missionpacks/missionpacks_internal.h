/* Shared private helpers for the Q2 missionpacks slice (non-monsters).
 * The whole slice is ported against the landed qa_q2f / qa_q2w / qa_q2m
 * mirrors of foundation/host.ts used by foundation/weapons and
 * foundation/monsters, because TS Q2Weapons/Q2Ballistics/MonsterContext
 * exist in C only in that system. GPL-2.0-or-later. */
#ifndef QA_Q2MP_INTERNAL_H
#define QA_Q2MP_INTERNAL_H
#include <math.h>
#include <string.h>
#include "qa/content/q2/foundation/monsters/types.h"
#include "qa/content/q2/foundation/weapons/ballistics.h"
#include "qa/core/math.h"

static inline qa_vec3 q2mp_v3(double x, double y, double z) { qa_vec3 v = {x, y, z}; return v; }
static inline qa_vec3 q2mp_zero(void) { return q2mp_v3(0, 0, 0); }
static inline double q2mp_now(qa_q2f_game *g) { return g->host->now(g->host->ctx); }
static inline double q2mp_rnd(qa_q2f_game *g) { return g->host->random(g->host->ctx); }
static inline double q2mp_frame(qa_q2f_game *g) { return g->host->frame_seconds(g->host->ctx); }
static inline bool q2mp_rr(qa_q2f_game *g) { return g->options.edition == QA_Q2F_RERELEASE; }
static inline bool q2mp_dm(qa_q2f_game *g) { return strcmp(g->options.mode, "deathmatch") == 0; }
static inline bool q2mp_coop(qa_q2f_game *g) { return strcmp(g->options.mode, "coop") == 0; }
static inline qa_body_state q2mp_body(qa_q2f_game *g, qa_q2f_entity *e) { return g->body(g->ctx, e->actor); }
static inline bool q2mp_live(qa_q2f_game *g, qa_actor_id a) { return g->host->actors.is_live(g->host->actors.ctx, a); }
static inline qa_q2f_entity *q2mp_ent(qa_q2f_game *g, bool has, qa_actor_id a) {
  return has ? g->entity(g->ctx, &a) : g->entity(g->ctx, NULL);
}
static inline qa_body_state_result q2mp_bread(qa_q2f_game *g, qa_actor_id a) {
  return g->host->bodies.read(g->host->bodies.ctx, a);
}
static inline qa_combat_state_result q2mp_cread(qa_q2f_game *g, qa_actor_id a) {
  return g->host->combat.read(g->host->combat.ctx, a);
}
static inline double q2mp_health(qa_q2f_game *g, qa_actor_id a) {
  qa_combat_state_result r = q2mp_cread(g, a);
  return r.has_value ? r.value.health : 0;
}
static inline bool q2mp_can_hurt(qa_q2f_game *g, qa_actor_id a) {
  qa_combat_state_result r = q2mp_cread(g, a);
  return r.has_value && r.value.can_take_damage;
}
static inline qa_trace_result q2mp_trace(qa_q2f_game *g, qa_vec3 start, qa_vec3 end, const qa_bounds *bounds,
                                         const qa_actor_id *ignore, double mask) {
  qa_q2f_trace_request r;
  memset(&r, 0, sizeof r);
  r.start = start; r.end = end; r.bounds = bounds; r.ignore = ignore; r.mask = mask;
  return g->host->trace(g->host->ctx, r);
}
static inline qa_vec3 q2mp_normal(qa_trace_result t) { return t.contact.has_plane ? t.contact.plane.normal : q2mp_zero(); }
static inline qa_vec3 q2mp_cnormal(const qa_touch_contact *c) { return c->plane ? c->plane->normal : q2mp_zero(); }
static inline double q2mp_surf(const qa_touch_contact *c) { return c->surface ? c->surface->native_flags : 0; }
static inline void q2mp_move(qa_q2f_game *g, qa_q2f_entity *e, qa_q2f_body_changes c, bool has_link, bool link) {
  g->move(g->ctx, e, c, has_link, link);
}
static inline qa_q2f_body_changes q2mp_changes(void) { qa_q2f_body_changes c; memset(&c, 0, sizeof c); return c; }
static inline void q2mp_sound(qa_q2f_game *g, qa_q2f_entity *e, const char *path, bool hc, double ch,
                              bool hv, double vol, bool ha, double att) {
  g->sound(g->ctx, e->actor, path, hc, ch, hv, vol, ha, att);
}
static inline void q2mp_damage(qa_q2f_game *g, qa_actor_id target, qa_q2f_entity *inf_ent, qa_actor_id inf_actor,
                               const qa_actor_id *attacker, double amount, double kick, qa_vec3 dir, qa_vec3 point,
                               qa_vec3 normal, double mod, bool has_flags, double flags, const char *weapon) {
  qa_q2f_damage_args a;
  memset(&a, 0, sizeof a);
  a.target = target; a.inflictor_entity = inf_ent; a.inflictor_actor = inf_actor; a.attacker = attacker;
  a.amount = amount; a.knockback = kick; a.direction = dir; a.point = point; a.normal = normal;
  a.means_of_death = mod; a.has_flags = has_flags; a.flags = flags; a.weapon = weapon;
  (void)g->damage(g->ctx, a);
}
static inline void q2mp_radius(qa_q2f_game *g, qa_q2f_entity *inf, const qa_actor_id *attacker, double damage,
                               const qa_actor_id *ignore, double radius, double mod, bool has_flags, double flags,
                               const char *weapon) {
  qa_q2f_radius_damage_args a;
  memset(&a, 0, sizeof a);
  a.inflictor = inf; a.attacker = attacker; a.damage = damage; a.ignore = ignore; a.radius = radius;
  a.means_of_death = mod; a.has_damage_flags = has_flags; a.damage_flags = flags; a.weapon = weapon;
  g->radius_damage(g->ctx, a);
}
static inline void q2mp_combat_create(qa_q2f_game *g, qa_owned_actor actor, double health, double mass, bool can_take) {
  qa_combat_state s;
  memset(&s, 0, sizeof s);
  s.health = health; s.mass = mass; s.can_take_damage = can_take; s.invulnerable = false; s.team = NULL;
  s.armor.regular.kind = QA_REGULAR_ARMOR_NONE; s.armor.powered.kind = "none";
  g->host->combat.create(g->host->combat.ctx, actor, s);
}
static inline void q2mp_traits_hurt(qa_q2f_game *g, qa_owned_actor actor, bool can_take) {
  qa_q2f_combat_traits t;
  memset(&t, 0, sizeof t);
  t.has_can_take_damage = true; t.can_take_damage = can_take;
  g->host->combat.set_traits(g->host->combat.ctx, actor, t);
}
static inline void q2mp_traits_mass(qa_q2f_game *g, qa_owned_actor actor, double mass) {
  qa_q2f_combat_traits t;
  memset(&t, 0, sizeof t);
  t.has_mass = true; t.mass = mass;
  g->host->combat.set_traits(g->host->combat.ctx, actor, t);
}
static inline uint32_t q2mp_u32(double v) { return (uint32_t)(int64_t)trunc(v); }
static inline bool q2mp_touch_eq(qa_q2f_touch a, qa_q2f_touch b) { return a.fn == b.fn && a.ctx == b.ctx; }
static inline bool q2mp_use_eq(qa_q2f_use a, qa_q2f_use b) { return a.fn == b.fn && a.ctx == b.ctx; }
static inline bool q2mp_die_eq(qa_q2f_die a, qa_q2f_die b) { return a.fn == b.fn && a.ctx == b.ctx; }
static inline bool q2mp_think_is(qa_q2f_think a, qa_q2f_think b) { return a.fn == b.fn && a.ctx == b.ctx; }
static inline qa_angle_vectors q2mp_axes(qa_vec3 a) { return qa_math_angle_vectors_rule(a, QA_ANGLE_RULE_NATIVE); }
static inline qa_vec3 q2mp_vta(qa_vec3 v) { return qa_vector_to_angles_with_rule(v, QA_VECTOR_TO_ANGLES_NATIVE); }
#define q2mp_add qa_add3_double
#define q2mp_sub qa_sub3_double
#define q2mp_scale qa_scale3_double
#define q2mp_dot qa_dot3_double
#define q2mp_len qa_hypot3
#define q2mp_norm qa_normalize3_double_hypot
#endif
