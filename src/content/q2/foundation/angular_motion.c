/* Port of src/content/q2/foundation/angular-motion.ts. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/angular_motion.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q2/foundation/checkpoint.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/foundation/support.h"

typedef struct move { qa_q2_entity *e; qa_vec3 destination; double speed; qa_q2_think done; } move;
struct qa_q2_angular_motion {
  char *prefix;
  move *moves; size_t count, cap;
  qa_q2_named_think names[3];
  char *name_buf[3];
  qa_q2_callback_definitions defs;
  qa_q2_angular_motion_entry *cp; size_t cp_count;
};

static move *find_move(qa_q2_angular_motion *m, qa_q2_entity *e) {
  for (size_t i = 0; i < m->count; i++) if (m->moves[i].e == e) return &m->moves[i];
  return NULL;
}
static move *state_of(qa_q2_angular_motion *m, qa_q2_entity *e) {
  move *s = find_move(m, e);
  if (!s) qa_range_error("Q2 angular callback has no active move");
  return s;
}
static void delete_move(qa_q2_angular_motion *m, qa_q2_entity *e) {
  for (size_t i = 0; i < m->count; i++)
    if (m->moves[i].e == e) { memmove(&m->moves[i], &m->moves[i + 1], (m->count - i - 1) * sizeof *m->moves); m->count--; return; }
}

static void done_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_angular_motion *m = c;
  move st = *state_of(m, e);
  delete_move(m, e);
  e->angular_velocity = QA_Q2_ZERO;
  g->motion(g->ctx, e, e->motion);
  if (st.done.fn) st.done.fn(st.done.ctx, e, g);
}
static void final_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_angular_motion *m = c;
  qa_vec3 delta = qa_q2_subtract(state_of(m, e)->destination, qa_q2_game_body(g, e).angles);
  if (delta.x == 0 && delta.y == 0 && delta.z == 0) { done_fn(m, e, g); return; }
  e->angular_velocity = qa_q2_scale(delta, 1 / g->host->frame_seconds(g->host->ctx));
  g->motion(g->ctx, e, e->motion);
  g->schedule(g->ctx, e, g->host->frame_seconds(g->host->ctx), (qa_q2_think){m, done_fn});
}
static void begin_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_angular_motion *m = c;
  move *st = state_of(m, e);
  if (st->speed < e->speed) st->speed = fmin(e->speed, st->speed + e->accel);
  qa_vec3 delta = qa_q2_subtract(st->destination, qa_q2_game_body(g, e).angles);
  double time = qa_q2_length(delta) / st->speed;
  double frame = g->host->frame_seconds(g->host->ctx);
  if (time < frame) { final_fn(m, e, g); return; }
  e->angular_velocity = qa_q2_scale(delta, 1 / time);
  g->motion(g->ctx, e, e->motion);
  if (st->speed >= e->speed) g->schedule(g->ctx, e, floor(time / frame) * frame, (qa_q2_think){m, final_fn});
  else g->schedule(g->ctx, e, frame, (qa_q2_think){m, begin_fn});
}

qa_q2_angular_motion *qa_q2_angular_motion_create(const char *prefix) {
  qa_q2_angular_motion *m = calloc(1, sizeof *m);
  if (!m) return NULL;
  const char *p = prefix ? prefix : "q2:angular";
  m->prefix = malloc(strlen(p) + 1);
  if (!m->prefix) { free(m); return NULL; }
  strcpy(m->prefix, p);
  static const char *suffixes[3] = {"AngleMove_Done", "AngleMove_Final", "AngleMove_Begin"};
  void (*fns[3])(void *, qa_q2_entity *, qa_q2_game_services *) = {done_fn, final_fn, begin_fn};
  for (int i = 0; i < 3; i++) {
    size_t n = strlen(p) + strlen(suffixes[i]) + 2;
    m->name_buf[i] = malloc(n);
    if (!m->name_buf[i]) { qa_q2_angular_motion_destroy(m); return NULL; }
    snprintf(m->name_buf[i], n, "%s/%s", p, suffixes[i]);
    m->names[i] = (qa_q2_named_think){m->name_buf[i], {m, fns[i]}};
  }
  m->defs.think = m->names; m->defs.think_count = 3;
  return m;
}
void qa_q2_angular_motion_destroy(qa_q2_angular_motion *m) {
  if (!m) return;
  free(m->prefix); free(m->moves); free(m->cp);
  for (int i = 0; i < 3; i++) free(m->name_buf[i]);
  free(m);
}
qa_q2_callback_definitions qa_q2_angular_motion_callbacks(qa_q2_angular_motion *m) { return m->defs; }

qa_q2_angular_motion_checkpoint qa_q2_angular_motion_capture(qa_q2_angular_motion *m, qa_q2_game_services *g) {
  free(m->cp); m->cp = NULL; m->cp_count = 0;
  size_t n = 0;
  qa_q2_entity *const *entities = g->entities(g->ctx, &n);
  m->cp = malloc((n ? n : 1) * sizeof *m->cp);
  if (!m->cp) qa_range_error("out of memory");
  for (size_t i = 0; i < n; i++) {
    move *st = find_move(m, entities[i]);
    if (!st) continue;
    const char *done = qa_q2_think_name(g->source_callbacks, st->done, st->done.fn != NULL);
    if (!done) qa_range_error("Q2 angular move checkpoint has no end function");
    m->cp[m->cp_count++] = (qa_q2_angular_motion_entry){
      {(double)entities[i]->actor.id.slot, (double)entities[i]->actor.id.generation}, st->destination, st->speed, done};
  }
  return (qa_q2_angular_motion_checkpoint){m->cp, m->cp_count};
}

void qa_q2_angular_motion_restore(qa_q2_angular_motion *m, qa_q2_game_services *g, qa_q2_angular_motion_checkpoint cp) {
  m->count = 0;
  for (size_t i = 0; i < cp.count; i++) {
    qa_owned_actor owner = qa_q2_restore_actor(g, cp.entries[i].actor);
    qa_q2_entity *e = g->entity(g->ctx, &owner.id);
    bool has = false;
    qa_q2_think done = qa_q2_think_resolve(g->source_callbacks, cp.entries[i].done, &has);
    if (!e || !has) qa_range_error("Q2 angular move checkpoint has no actor or end function");
    if (m->count == m->cap) { m->cap = m->cap ? m->cap * 2 : 8; m->moves = realloc(m->moves, m->cap * sizeof *m->moves); if (!m->moves) qa_range_error("out of memory"); }
    m->moves[m->count++] = (move){e, cp.entries[i].destination, cp.entries[i].speed, done};
  }
}

void qa_q2_angular_motion_move_to(qa_q2_angular_motion *m, qa_q2_entity *e, qa_q2_game_services *g, qa_vec3 dest, qa_q2_think done) {
  qa_q2_source_callbacks_register(g->source_callbacks, m->defs);
  move *st = find_move(m, e);
  if (!st) {
    if (m->count == m->cap) { m->cap = m->cap ? m->cap * 2 : 8; m->moves = realloc(m->moves, m->cap * sizeof *m->moves); if (!m->moves) qa_range_error("out of memory"); }
    st = &m->moves[m->count++];
    st->e = e;
  }
  st->destination = dest; st->done = done;
  st->speed = (g->options.edition == QA_Q2_EDITION_RERELEASE && e->accel != e->speed) ? 0 : e->speed;
  e->angular_velocity = QA_Q2_ZERO;
  g->motion(g->ctx, e, e->motion);
  qa_actor_id master = e->has_team_master ? e->team_master : e->actor.id;
  if (g->has_current_actor && qa_same_actor(g->current_actor, master)) begin_fn(m, e, g);
  else g->schedule(g->ctx, e, g->host->frame_seconds(g->host->ctx), (qa_q2_think){m, begin_fn});
}
