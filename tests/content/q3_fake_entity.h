/* Test-only GameEntityBinding over local storage: the role Q3EntityRecords
 * plays in the TS tests (body table, combat health/takedamage), without a
 * session. GPL-2.0-or-later. */
#ifndef QA_TESTS_CONTENT_Q3_FAKE_ENTITY_H
#define QA_TESTS_CONTENT_Q3_FAKE_ENTITY_H
#include <stdbool.h>
#include <string.h>
#include "qa/content/q3/base/game/state.h"

typedef struct q3_fake_entity {
  bool active;
  qa_owned_actor actor;
  qa_body_state body; qa_actor_id ground; /* body.ground points at ground */
  bool linked; qa_linked_body link;
  double health; bool takedamage;
  double nextthink; int schedules;
} q3_fake_entity;

static inline qa_body_state q3_fake_body_read(void *ctx) {
  q3_fake_entity *f = ctx; qa_body_state body = f->body;
  if (body.ground != NULL) body.ground = &f->ground;
  return body;
}
static inline void q3_fake_body_write(void *ctx, qa_body_state value) {
  q3_fake_entity *f = ctx;
  if (value.ground != NULL) { f->ground = *value.ground; value.ground = &f->ground; }
  f->body = value;
}
static inline bool q3_fake_body_linked(void *ctx, qa_linked_body *out) {
  q3_fake_entity *f = ctx;
  if (!f->linked) return false;
  *out = f->link; out->state = q3_fake_body_read(ctx);
  return true;
}
static inline qa_owned_actor q3_fake_actor(void *ctx) { return ((q3_fake_entity *)ctx)->actor; }
static inline bool q3_fake_active(void *ctx) { return ((q3_fake_entity *)ctx)->active; }
static inline double q3_fake_health(void *ctx) { return ((q3_fake_entity *)ctx)->health; }
static inline void q3_fake_set_health(void *ctx, double v) { ((q3_fake_entity *)ctx)->health = v; }
static inline bool q3_fake_takedamage(void *ctx) { return ((q3_fake_entity *)ctx)->takedamage; }
static inline void q3_fake_set_takedamage(void *ctx, bool v) { ((q3_fake_entity *)ctx)->takedamage = v; }
static inline void q3_fake_schedule(void *ctx, double v) { q3_fake_entity *f = ctx; f->nextthink = v; f->schedules++; }

/* Binding whose every member reads and writes *f. */
static inline qa_q3_game_entity_binding q3_fake_binding(q3_fake_entity *f) {
  qa_q3_game_entity_binding b; memset(&b, 0, sizeof b);
  b.ctx = f; b.body.ctx = f;
  b.body.read = q3_fake_body_read; b.body.write = q3_fake_body_write; b.body.linked = q3_fake_body_linked;
  b.actor = q3_fake_actor; b.active = q3_fake_active;
  b.health = q3_fake_health; b.set_health = q3_fake_set_health;
  b.takedamage = q3_fake_takedamage; b.set_takedamage = q3_fake_set_takedamage;
  b.schedule = q3_fake_schedule;
  return b;
}
/* Fresh entity over a zeroed fake: TS new GameEntity(slot, binding). */
static inline void q3_fake_entity_init(qa_q3_game_entity *e, q3_fake_entity *f, double slot) {
  memset(f, 0, sizeof *f);
  qa_q3_game_entity_init(e, slot, q3_fake_binding(f));
}
#endif
