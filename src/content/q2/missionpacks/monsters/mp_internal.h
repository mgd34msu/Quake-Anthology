/* Shared private helpers for the Q2 mission-pack monsters slice.
 * GPL-2.0-or-later. */
#ifndef QA_Q2MPM_INTERNAL_H
#define QA_Q2MPM_INTERNAL_H
#include <math.h>
#include <string.h>

#include "qa/content/q2/missionpacks/monsters/types.h"
#include "qa/core/math.h"

static inline qa_vec3 q2mpm_v3(double x, double y, double z) { qa_vec3 v = {x, y, z}; return v; }
static inline qa_vec3 q2mpm_zero(void) { return q2mpm_v3(0, 0, 0); }
static inline double q2mpm_now(qa_q2_game_services *g) { return g->host->now(g->host->ctx); }
static inline double q2mpm_rnd(qa_q2_game_services *g) { return g->host->random(g->host->ctx); }
static inline bool q2mpm_rr(qa_q2_game_services *g) { return g->options.edition == QA_Q2_EDITION_RERELEASE; }
static inline qa_body_state q2mpm_body(qa_q2_game_services *g, qa_q2_entity *e) { return g->body(g->ctx, e->actor); }
static inline bool q2mpm_live(qa_q2_game_services *g, qa_actor_id a) { return qa_session_actor_registry_is_live(g->host->actors, a); }
static inline void q2mpm_set_move(qa_q2m_context *c, const char *name) { c->set_move(c->impl_ctx, c, name, false, false); }
static inline qa_trace_result q2mpm_trace(qa_q2_game_services *g, qa_vec3 start, qa_vec3 end, const qa_bounds *bounds,
                                          const qa_actor_id *ignore, double mask) {
  qa_q2_trace_request r;
  memset(&r, 0, sizeof r);
  r.start = start; r.end = end; r.bounds = bounds; r.ignore = ignore; r.mask = mask;
  return g->host->trace(g->host->ctx, r);
}
/* TS game.sound(entity, path, channel?, volume?, attenuation?). */
static inline void q2mpm_sound(qa_q2_game_services *g, qa_q2_entity *e, const char *path, bool has_ch, double ch,
                               bool has_vol, double vol, bool has_att, double att) {
  /* TS defaults: channel 2, volume 1, attenuation 1. */
  g->sound(g->ctx, e->actor, path, has_ch ? ch : 2, has_vol ? vol : 1, has_att ? att : 1);
}
static inline void q2mpm_snd(qa_q2m_context *c, const char *path, double ch) {
  q2mpm_sound(c->game, c->entity, path, true, ch, false, 0, false, 0);
}
static inline void q2mpm_snd4(qa_q2m_context *c, const char *path, double ch, double vol, double att) {
  q2mpm_sound(c->game, c->entity, path, true, ch, true, vol, true, att);
}
static inline void q2mpm_move_body(qa_q2_game_services *g, qa_q2_entity *e, qa_q2_body_changes ch, bool has_link, bool link) {
  g->move(g->ctx, e, ch, has_link ? link : true); /* TS link = true */
}
static inline void q2mpm_set_damageable(qa_q2_game_services *g, qa_q2_entity *e, bool can) {
  qa_combat_trait_changes t;
  memset(&t, 0, sizeof t);
  t.has_can_take_damage = true; t.can_take_damage = can;
  qa_gameplay_authority_set_traits(g->host->combat, e->actor, t);
}
static inline void q2mpm_emit_effect(qa_q2_game_services *g, const char *effect, qa_vec3 origin, qa_vec3 direction,
                                     double count, double color) {
  qa_q2_presentation_event e;
  memset(&e, 0, sizeof e);
  e.kind = "effect"; e.effect = effect; e.origin = origin; e.direction = direction;
  e.count = count; e.color = color;
  g->host->emit(g->host->ctx, e);
}
/* TS game.damage(target, inflictor, attacker, amount, knockback,
 * direction, point, normal, means, flags?). */
static inline qa_damage_outcome q2mpm_damage(qa_q2_game_services *g, qa_actor_id target, qa_q2_entity *inflictor,
                                             const qa_actor_id *attacker, double amount, double knockback,
                                             qa_vec3 direction, qa_vec3 point, qa_vec3 normal, double means,
                                             bool has_flags, double flags) {
  qa_actor_id inflictor_id = {0};
  return g->damage(g->ctx, target, inflictor, inflictor_id, attacker, amount, knockback, direction, point, normal,
                   means, has_flags ? flags : 0, NULL); /* TS flags = 0, weapon = null */
}
/* TS recordAt over typed arrays (index validated by qa_q2m_record_at). */
static inline double q2mpm_at_d(const double *values, size_t count, double index) {
  return values[qa_q2m_record_at(count, index)];
}
static inline qa_vec3 q2mpm_at_v(const qa_vec3 *values, size_t count, double index) {
  return values[qa_q2m_record_at(count, index)];
}
static inline const char *q2mpm_at_s(const char *const *values, size_t count, double index) {
  return values[qa_q2m_record_at(count, index)];
}
static inline qa_actor_id q2mpm_at_a(const qa_actor_id *values, size_t count, double index) {
  return values[qa_q2m_record_at(count, index)];
}
/* JS bitwise ops on doubles go through int32. */
static inline double q2mpm_or(double value, int32_t bits) { return (double)((int32_t)value | bits); }
static inline double q2mpm_and(double value, int32_t bits) { return (double)((int32_t)value & bits); }
static inline bool q2mpm_has(double value, int32_t bits) { return ((int32_t)value & bits) != 0; }

/* Merged callback table for TS {...base.callbacks, ...extra}: extra
 * entries replace same-named base entries. Allocated in the module arena. */
qa_q2m_named_handler *qa_q2mpm_merge_callbacks(qa_q2mpm_arena *arena, const qa_q2m_named_handler *base, size_t base_count,
                                               const qa_q2m_named_handler *extra, size_t extra_count,
                                               size_t *out_count);
const qa_q2m_handler *qa_q2mpm_find_callback(const qa_q2m_named_handler *table, size_t count, const char *name);

#endif
