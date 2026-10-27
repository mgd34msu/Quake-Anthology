/* Shared call-shape helpers for the rerelease slice. Internal. */
#ifndef QA_Q2RR_INTERNAL_H
#define QA_Q2RR_INTERNAL_H
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/content/q2/rerelease/types.h"
#include "qa/world/actors/body.h"
#include "qa/world/actors/registry.h"
#include "qa/world/gameplay/authority.h"
#include "qa/world/gameplay/inventory.h"
#include <math.h>
#include <string.h>

static inline double rr_health(qa_q2_game_services *g, qa_actor_id a) {
  qa_combat_state_result r = qa_gameplay_authority_read(g->host->combat, a);
  return r.has_value ? r.value.health : 0;
}
static inline bool rr_can_take_damage(qa_q2_game_services *g, qa_actor_id a) {
  qa_combat_state_result r = qa_gameplay_authority_read(g->host->combat, a);
  return r.has_value && r.value.can_take_damage;
}
static inline qa_q2_entity *rr_ent(qa_q2_game_services *g, const qa_actor_id *a) {
  return a ? g->entity(g->ctx, a) : NULL;
}
static inline qa_body_state rr_body_of(qa_q2_game_services *g, qa_q2_entity *e) {
  return g->body(g->ctx, e->actor);
}
static inline double rr_now(qa_q2_game_services *g) { return g->host->now(g->host->ctx); }
static inline double rr_frame(qa_q2_game_services *g) { return g->host->frame_seconds(g->host->ctx); }
static inline void rr_diag(qa_q2_game_services *g, const char *m) { g->host->diagnostic(g->host->ctx, m); }
static inline void rr_sound_ent(qa_q2_game_services *g, qa_q2_entity *e, const char *path, double ch, double vol, double att) {
  g->sound(g->ctx, e->actor, path, ch, vol, att);
}
static inline void rr_emit_host(qa_q2_game_services *g, qa_q2_presentation_event ev) { g->host->emit(g->host->ctx, ev); }
static inline void rr_emit_rr(qa_q2_rerelease_hooks *h, qa_q2_rerelease_event ev) { h->emit(h->ctx, ev); }
static inline qa_vec3 rr_v3(double x, double y, double z) { qa_vec3 v = {x, y, z}; return v; }
/* TS int32 bitwise on doubles. */
static inline long rr_i32(double v) { return (long)(int32_t)v; }
static inline double rr_band(double a, double b) { return (double)((int32_t)a & (int32_t)b); }
static inline double rr_bor(double a, double b) { return (double)((int32_t)a | (int32_t)b); }
static inline double rr_bxor(double a, double b) { return (double)((int32_t)a ^ (int32_t)b); }
static inline double rr_bnot_and(double a, double b) { return (double)((int32_t)a & ~(int32_t)b); }
#endif
