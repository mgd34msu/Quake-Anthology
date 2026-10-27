/* Shared private helpers for the Q2 base monsters slice (mirrors the
 * foundation monsters_internal.h conventions for qa_q2f_*). GPL-2.0-or-later. */
#ifndef QA_Q2BM_BASE_INTERNAL_H
#define QA_Q2BM_BASE_INTERNAL_H
#include <string.h>
#include "qa/content/q2/foundation/monsters/types.h"
#include "qa/core/math.h"

static inline qa_vec3 q2bm_v3(double x, double y, double z) { qa_vec3 v = {x, y, z}; return v; }
static inline qa_vec3 q2bm_zero(void) { return q2bm_v3(0, 0, 0); }
static inline double q2bm_now(qa_q2f_game *g) { return g->host->now(g->host->ctx); }
static inline double q2bm_rnd(qa_q2f_game *g) { return g->host->random(g->host->ctx); }
static inline qa_body_state q2bm_body(qa_q2f_game *g, qa_q2f_entity *e) { return g->body(g->ctx, e->actor); }
static inline void q2bm_set_move(qa_q2m_context *c, const char *name) { c->set_move(c->impl_ctx, c, name, false, false); }
static inline void q2bm_sound(qa_q2f_game *g, qa_q2f_entity *e, const char *path, bool has_ch, double ch,
                              bool has_vol, double vol, bool has_att, double att) {
  g->sound(g->ctx, e->actor, path, has_ch, ch, has_vol, vol, has_att, att);
}
static inline qa_trace_result q2bm_trace(qa_q2f_game *g, qa_vec3 start, qa_vec3 end, const qa_bounds *bounds,
                                         const qa_actor_id *ignore, double mask) {
  qa_q2f_trace_request r;
  memset(&r, 0, sizeof r);
  r.start = start; r.end = end; r.bounds = bounds; r.ignore = ignore; r.mask = mask;
  return g->host->trace(g->host->ctx, r);
}
static inline void q2bm_move_body(qa_q2f_game *g, qa_q2f_entity *e, qa_q2f_body_changes c, bool has_link, bool link) {
  g->move(g->ctx, e, c, has_link, link);
}
static inline void q2bm_set_traits_damage(qa_q2f_game *g, qa_q2f_entity *e, bool can_take_damage) {
  qa_q2f_combat_traits traits;
  memset(&traits, 0, sizeof traits);
  traits.has_can_take_damage = true; traits.can_take_damage = can_take_damage;
  g->host->combat.set_traits(g->host->combat.ctx, e->actor, traits);
}
static inline void q2bm_emit_effect(qa_q2f_game *g, const char *effect, qa_vec3 origin, qa_vec3 direction,
                                    double count, double color) {
  qa_q2f_presentation_event e;
  memset(&e, 0, sizeof e);
  e.kind = QA_Q2F_EVENT_EFFECT; e.effect = effect; e.origin = origin; e.direction = direction;
  e.count = count; e.color = color;
  g->host->emit(g->host->ctx, e);
}
static inline void q2bm_emit_loop(qa_q2f_game *g, qa_q2f_entity *entity, const char *path, const char *loop,
                                  double volume) {
  qa_q2f_presentation_event e;
  memset(&e, 0, sizeof e);
  e.kind = QA_Q2F_EVENT_SOUND; e.actor = &entity->actor.id;
  e.origin = q2bm_body(g, entity).origin; e.path = path; e.channel = 0;
  e.volume = volume; e.attenuation = 1; e.reliable = false; e.loop = loop;
  g->host->emit(g->host->ctx, e);
}
#endif
