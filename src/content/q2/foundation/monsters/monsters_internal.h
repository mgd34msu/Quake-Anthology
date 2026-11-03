/* Shared private helpers for the Q2 foundation monsters slice.
 * fields.ts add/subtract/scale/dot/length/normalize/zero are the
 * core/math double variants. GPL-2.0-or-later. */
#ifndef QA_Q2M_INTERNAL_H
#define QA_Q2M_INTERNAL_H
#include <string.h>
#include "qa/content/q2/foundation/monsters/types.h"
#include "qa/core/math.h"

static inline qa_vec3 q2m_v3(double x, double y, double z) { qa_vec3 v = {x, y, z}; return v; }
static inline qa_vec3 q2m_zero(void) { return q2m_v3(0, 0, 0); }
static inline double q2m_now(qa_q2_game_services *g) { return g->host->now(g->host->ctx); }
static inline double q2m_rnd(qa_q2_game_services *g) { return g->host->random(g->host->ctx); }
static inline bool q2m_rr(qa_q2_game_services *g) { return g->options.edition == QA_Q2_EDITION_RERELEASE; }
static inline qa_trace_result q2m_trace(qa_q2_game_services *g, qa_vec3 start, qa_vec3 end, const qa_bounds *bounds,
                                        const qa_actor_id *ignore, double mask) {
  qa_q2_trace_request r;
  memset(&r, 0, sizeof r);
  r.start = start; r.end = end; r.bounds = bounds; r.ignore = ignore; r.mask = mask;
  return g->host->trace(g->host->ctx, r);
}
static inline qa_body_state q2m_body(qa_q2_game_services *g, qa_q2_entity *e) { return g->body(g->ctx, e->actor); }
static inline bool q2m_live(qa_q2_game_services *g, qa_actor_id a) { return qa_session_actor_registry_is_live(g->host->actors, a); }
static inline void q2m_sound(qa_q2_game_services *g, qa_q2_entity *e, const char *path, bool has_ch, double ch,
                             bool has_vol, double vol, bool has_att, double att) {
  /* TS defaults: channel 2, volume 1, attenuation 1. */
  g->sound(g->ctx, e->actor, path, has_ch ? ch : 2, has_vol ? vol : 1, has_att ? att : 1);
}
static inline void q2m_move(qa_q2_game_services *g, qa_q2_entity *e, qa_q2_body_changes c, bool has_link, bool link) {
  g->move(g->ctx, e, c, has_link ? link : true); /* TS link = true */
}
#endif
