/* Port of src/content/q2/foundation/motion.ts. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/motion.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q2/foundation/checkpoint.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/foundation/support.h"

typedef struct curve { float *positions; size_t count; double frame, subframe, subframes; } curve;
typedef struct move {
  qa_q2_entity *e; qa_vec3 direction, destination, reference;
  double remaining, current_speed, move_speed, next_speed, decel_distance;
  qa_q2_think done; curve *cv;
} move;
struct qa_q2_linear_motion {
  char *prefix;
  move *moves; size_t count, cap;
  qa_q2_named_think names[5];
  char *name_buf[5];
  qa_q2_callback_definitions defs;
  qa_q2_linear_motion_entry *cp; size_t cp_count;
  double **cp_positions;
};

static move *find_move(qa_q2_linear_motion *m, qa_q2_entity *e) {
  for (size_t i = 0; i < m->count; i++) if (m->moves[i].e == e) return &m->moves[i];
  return NULL;
}
static move *state_of(qa_q2_linear_motion *m, qa_q2_entity *e) {
  move *s = find_move(m, e);
  if (!s) qa_range_error("Q2 mover callback has no active move");
  return s;
}
static void free_curve(move *mv) { if (mv->cv) { free(mv->cv->positions); free(mv->cv); mv->cv = NULL; } }
static void delete_move(qa_q2_linear_motion *m, qa_q2_entity *e) {
  for (size_t i = 0; i < m->count; i++)
    if (m->moves[i].e == e) { free_curve(&m->moves[i]); memmove(&m->moves[i], &m->moves[i + 1], (m->count - i - 1) * sizeof *m->moves); m->count--; return; }
}
static void set_velocity(qa_q2_entity *e, qa_q2_game_services *g, qa_vec3 v) {
  qa_q2_body_changes c = {0};
  c.has_velocity = true; c.velocity = v;
  g->move(g->ctx, e, c, false);
  g->motion(g->ctx, e, e->motion);
}

static double accel_distance(double target, double rate) { return target * (target / rate + 1) / 2; }
static void calculate_acceleration(move *st, qa_q2_entity *e) {
  st->move_speed = e->speed;
  if (st->remaining < e->accel) { st->current_speed = st->remaining; return; }
  double ad = accel_distance(e->speed, e->accel);
  double dd = accel_distance(e->speed, e->decel);
  if (st->remaining - ad - dd < 0) {
    double factor = (e->accel + e->decel) / (e->accel * e->decel);
    st->move_speed = (-2 + sqrt(4 + 8 * factor * st->remaining)) / (2 * factor);
    dd = accel_distance(st->move_speed, e->decel);
  }
  st->decel_distance = dd;
}
static void accelerate_state(move *st, qa_q2_entity *e) {
  if (st->remaining <= st->decel_distance) {
    if (st->remaining < st->decel_distance) {
      if (st->next_speed != 0) { st->current_speed = st->next_speed; st->next_speed = 0; return; }
      if (st->current_speed > e->decel) st->current_speed -= e->decel;
    }
    return;
  }
  if (st->current_speed == st->move_speed && st->remaining - st->current_speed < st->decel_distance) {
    double first = st->remaining - st->decel_distance;
    double second = st->move_speed * (1 - first / st->move_speed);
    st->current_speed = st->move_speed;
    st->next_speed = st->move_speed - e->decel * second / (first + second);
    return;
  }
  if (st->current_speed < e->speed) {
    double old = st->current_speed;
    st->current_speed = fmin(st->current_speed + e->accel, e->speed);
    if (st->remaining - st->current_speed >= st->decel_distance) return;
    double first = st->remaining - st->decel_distance;
    double first_speed = (old + st->move_speed) / 2;
    double second = st->move_speed * (1 - first / first_speed);
    double distance = first + second;
    st->current_speed = first_speed * first / distance + st->move_speed * second / distance;
    st->next_speed = st->move_speed - e->decel * second / distance;
  }
}

static void done_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_linear_motion *m = c;
  set_velocity(e, g, QA_Q2_ZERO);
  move st = *state_of(m, e);
  st.cv = NULL; /* freed by delete */
  delete_move(m, e);
  if (st.done.fn) st.done.fn(st.done.ctx, e, g);
}
static void final_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_linear_motion *m = c;
  move *st = state_of(m, e);
  if (st->remaining == 0) { done_fn(m, e, g); return; }
  qa_vec3 delta = g->options.edition == QA_Q2_EDITION_RERELEASE
      ? qa_q2_subtract(st->destination, qa_q2_game_body(g, e).origin)
      : qa_q2_scale(st->direction, st->remaining);
  set_velocity(e, g, qa_q2_scale(delta, 1 / g->host->frame_seconds(g->host->ctx)));
  g->schedule(g->ctx, e, g->host->frame_seconds(g->host->ctx), (qa_q2_think){m, done_fn});
}
static void begin_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_linear_motion *m = c;
  move *st = state_of(m, e);
  double frame = g->host->frame_seconds(g->host->ctx);
  if (e->speed * frame >= st->remaining) { final_fn(m, e, g); return; }
  set_velocity(e, g, qa_q2_scale(st->direction, e->speed));
  double frames = floor(st->remaining / e->speed / frame);
  st->remaining -= frames * e->speed * frame;
  g->schedule(g->ctx, e, frames * frame, (qa_q2_think){m, final_fn});
}
static void accelerate_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_linear_motion *m = c;
  move *st = state_of(m, e);
  if (g->options.edition == QA_Q2_EDITION_RERELEASE) st->remaining = qa_q2_length(qa_q2_subtract(st->destination, qa_q2_game_body(g, e).origin));
  else st->remaining -= st->current_speed;
  if (st->current_speed == 0) calculate_acceleration(st, e);
  accelerate_state(st, e);
  if (st->remaining <= st->current_speed) { final_fn(m, e, g); return; }
  set_velocity(e, g, qa_q2_scale(st->direction, st->current_speed / g->host->frame_seconds(g->host->ctx)));
  g->schedule(g->ctx, e, g->host->frame_seconds(g->host->ctx), (qa_q2_think){m, accelerate_fn});
}
static void curve_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_linear_motion *m = c;
  move *st = state_of(m, e);
  curve *cv = st->cv;
  if (!cv) qa_range_error("Q2 rerelease accelerated move has no curve");
  if (cv->subframes != 0 && cv->subframe == cv->subframes + 1) { cv->subframe = 0; cv->frame++; }
  if (cv->frame == (double)cv->count) { final_fn(m, e, g); return; }
  double distance;
  if (cv->subframes != 0) {
    int64_t fi = (int64_t)cv->frame;
    if (fi - 1 < 0 || fi >= (int64_t)cv->count) qa_range_error("Q2 accelerated move references a missing curve sample");
    double from = cv->positions[fi - 1], to = cv->positions[fi];
    distance = from + (to - from) * (cv->subframe + 1) / (cv->subframes + 1);
    cv->subframe++;
  } else {
    int64_t fi = (int64_t)cv->frame++;
    if (fi < 0 || fi >= (int64_t)cv->count) qa_range_error("Q2 accelerated move references a missing curve sample");
    distance = cv->positions[fi];
  }
  qa_vec3 target = qa_q2_add(st->reference, qa_q2_scale(st->direction, distance));
  set_velocity(e, g, qa_q2_scale(qa_q2_subtract(target, qa_q2_game_body(g, e).origin), 1 / g->host->frame_seconds(g->host->ctx)));
  g->schedule(g->ctx, e, g->host->frame_seconds(g->host->ctx), (qa_q2_think){m, curve_fn});
}

qa_q2_linear_motion *qa_q2_linear_motion_create(const char *prefix) {
  qa_q2_linear_motion *m = calloc(1, sizeof *m);
  if (!m) return NULL;
  const char *p = prefix ? prefix : "q2:linear";
  m->prefix = malloc(strlen(p) + 1);
  if (!m->prefix) { free(m); return NULL; }
  strcpy(m->prefix, p);
  static const char *suffixes[5] = {"Move_Done", "Move_Final", "Move_Begin", "Think_AccelMove", "Move_Accel_Curve"};
  void (*fns[5])(void *, qa_q2_entity *, qa_q2_game_services *) = {done_fn, final_fn, begin_fn, accelerate_fn, curve_fn};
  for (int i = 0; i < 5; i++) {
    size_t n = strlen(p) + strlen(suffixes[i]) + 2;
    m->name_buf[i] = malloc(n);
    if (!m->name_buf[i]) { qa_q2_linear_motion_destroy(m); return NULL; }
    snprintf(m->name_buf[i], n, "%s/%s", p, suffixes[i]);
    m->names[i] = (qa_q2_named_think){m->name_buf[i], {m, fns[i]}};
  }
  m->defs.think = m->names; m->defs.think_count = 5;
  return m;
}
void qa_q2_linear_motion_destroy(qa_q2_linear_motion *m) {
  if (!m) return;
  for (size_t i = 0; i < m->count; i++) free_curve(&m->moves[i]);
  if (m->cp_positions) { for (size_t i = 0; i < m->cp_count; i++) free(m->cp_positions[i]); free(m->cp_positions); }
  free(m->prefix); free(m->moves); free(m->cp);
  for (int i = 0; i < 5; i++) free(m->name_buf[i]);
  free(m);
}
bool qa_q2_linear_motion_destination(qa_q2_linear_motion *m, qa_q2_entity *e, qa_vec3 *out) {
  move *s = find_move(m, e);
  if (!s) return false;
  *out = s->destination;
  return true;
}
qa_q2_callback_definitions qa_q2_linear_motion_callbacks(qa_q2_linear_motion *m) { return m->defs; }

qa_q2_linear_motion_checkpoint qa_q2_linear_motion_capture(qa_q2_linear_motion *m, qa_q2_game_services *g) {
  if (m->cp_positions) { for (size_t i = 0; i < m->cp_count; i++) free(m->cp_positions[i]); free(m->cp_positions); m->cp_positions = NULL; }
  free(m->cp); m->cp = NULL; m->cp_count = 0;
  size_t n = 0;
  qa_q2_entity *const *entities = g->entities(g->ctx, &n);
  m->cp = malloc((n ? n : 1) * sizeof *m->cp);
  m->cp_positions = calloc(n ? n : 1, sizeof *m->cp_positions);
  if (!m->cp || !m->cp_positions) qa_range_error("out of memory");
  for (size_t i = 0; i < n; i++) {
    move *st = find_move(m, entities[i]);
    if (!st) continue;
    const char *done = qa_q2_think_name(g->source_callbacks, st->done, st->done.fn != NULL);
    if (!done) qa_range_error("Q2 linear move checkpoint has no end function");
    qa_q2_linear_motion_entry *en = &m->cp[m->cp_count];
    *en = (qa_q2_linear_motion_entry){0};
    en->actor = (qa_saved_actor_id){(double)entities[i]->actor.id.slot, (double)entities[i]->actor.id.generation};
    en->direction = st->direction; en->destination = st->destination; en->reference = st->reference;
    en->remaining = st->remaining; en->current_speed = st->current_speed; en->move_speed = st->move_speed;
    en->next_speed = st->next_speed; en->decel_distance = st->decel_distance; en->done = done;
    if (st->cv) {
      en->has_curve = true;
      double *pos = malloc((st->cv->count ? st->cv->count : 1) * sizeof *pos);
      if (!pos) qa_range_error("out of memory");
      for (size_t j = 0; j < st->cv->count; j++) pos[j] = (double)st->cv->positions[j];
      m->cp_positions[m->cp_count] = pos;
      en->curve = (qa_q2_linear_curve_checkpoint){pos, st->cv->count, st->cv->frame, st->cv->subframe, st->cv->subframes};
    }
    m->cp_count++;
  }
  return (qa_q2_linear_motion_checkpoint){m->cp, m->cp_count};
}

void qa_q2_linear_motion_restore(qa_q2_linear_motion *m, qa_q2_game_services *g, qa_q2_linear_motion_checkpoint cp) {
  for (size_t i = 0; i < m->count; i++) free_curve(&m->moves[i]);
  m->count = 0;
  for (size_t i = 0; i < cp.count; i++) {
    qa_owned_actor owner = qa_q2_restore_actor(g, cp.entries[i].actor);
    qa_q2_entity *e = g->entity(g->ctx, &owner.id);
    bool has = false;
    qa_q2_think done = qa_q2_think_resolve(g->source_callbacks, cp.entries[i].done, &has);
    if (!e || !has) qa_range_error("Q2 linear move checkpoint has no actor or end function");
    if (m->count == m->cap) { m->cap = m->cap ? m->cap * 2 : 8; m->moves = realloc(m->moves, m->cap * sizeof *m->moves); if (!m->moves) qa_range_error("out of memory"); }
    move *st = &m->moves[m->count++];
    *st = (move){e, cp.entries[i].direction, cp.entries[i].destination, cp.entries[i].reference,
        cp.entries[i].remaining, cp.entries[i].current_speed, cp.entries[i].move_speed,
        cp.entries[i].next_speed, cp.entries[i].decel_distance, done, NULL};
    if (cp.entries[i].has_curve) {
      curve *cv = calloc(1, sizeof *cv);
      if (!cv) qa_range_error("out of memory");
      cv->count = cp.entries[i].curve.position_count;
      cv->positions = malloc((cv->count ? cv->count : 1) * sizeof *cv->positions);
      if (!cv->positions) qa_range_error("out of memory");
      for (size_t j = 0; j < cv->count; j++) cv->positions[j] = (float)cp.entries[i].curve.positions[j];
      cv->frame = cp.entries[i].curve.frame; cv->subframe = cp.entries[i].curve.subframe; cv->subframes = cp.entries[i].curve.subframes;
      st->cv = cv;
    }
  }
}

void qa_q2_linear_motion_move_to(qa_q2_linear_motion *m, qa_q2_entity *e, qa_q2_game_services *g, qa_vec3 dest, qa_q2_think done) {
  qa_q2_source_callbacks_register(g->source_callbacks, m->defs);
  qa_vec3 origin = qa_q2_game_body(g, e).origin;
  qa_vec3 delta = qa_q2_subtract(dest, origin);
  double distance = qa_q2_length(delta);
  move *st = find_move(m, e);
  if (!st) {
    if (m->count == m->cap) { m->cap = m->cap ? m->cap * 2 : 8; m->moves = realloc(m->moves, m->cap * sizeof *m->moves); if (!m->moves) qa_range_error("out of memory"); }
    st = &m->moves[m->count++];
    *st = (move){0};
    st->e = e;
  } else free_curve(st);
  st->direction = distance == 0 ? QA_Q2_ZERO : qa_q2_scale(delta, 1 / distance);
  st->remaining = distance; st->destination = dest; st->reference = origin;
  st->current_speed = 0; st->move_speed = 0; st->next_speed = 0; st->decel_distance = 0; st->done = done; st->cv = NULL;
  set_velocity(e, g, QA_Q2_ZERO);
  double frame = g->host->frame_seconds(g->host->ctx);
  if (e->speed == e->accel && e->speed == e->decel) {
    qa_actor_id master = e->has_team_master ? e->team_master : e->actor.id;
    if (g->has_current_actor && qa_same_actor(g->current_actor, master)) begin_fn(m, e, g);
    else g->schedule(g->ctx, e, frame, (qa_q2_think){m, begin_fn});
    return;
  }
  if (g->options.edition == QA_Q2_EDITION_RERELEASE && frame != 0.1) {
    double subframes = 0.1 / frame - 1;
    double *positions = NULL;
    size_t pn = 0, pcap = 0;
    if (subframes != 0) { pcap = 8; positions = malloc(pcap * sizeof *positions); if (!positions) qa_range_error("out of memory"); positions[pn++] = 0; }
    while (st->remaining != 0) {
      if (st->current_speed == 0) calculate_acceleration(st, e);
      accelerate_state(st, e);
      if (st->remaining <= st->current_speed) break;
      st->remaining -= st->current_speed;
      if (pn == pcap) { pcap = pcap ? pcap * 2 : 8; positions = realloc(positions, pcap * sizeof *positions); if (!positions) qa_range_error("out of memory"); }
      positions[pn++] = distance - st->remaining;
    }
    if (subframes != 0) {
      if (pn == pcap) { pcap++; positions = realloc(positions, pcap * sizeof *positions); if (!positions) qa_range_error("out of memory"); }
      positions[pn++] = distance;
    }
    curve *cv = calloc(1, sizeof *cv);
    if (!cv) qa_range_error("out of memory");
    cv->count = pn;
    cv->positions = malloc((pn ? pn : 1) * sizeof *cv->positions);
    if (!cv->positions) qa_range_error("out of memory");
    for (size_t i = 0; i < pn; i++) cv->positions[i] = (float)positions[i];
    free(positions);
    cv->frame = subframes != 0 ? 1 : 0; cv->subframe = 0; cv->subframes = subframes;
    st->cv = cv;
    g->schedule(g->ctx, e, frame, (qa_q2_think){m, curve_fn});
    return;
  }
  g->schedule(g->ctx, e, frame, (qa_q2_think){m, accelerate_fn});
}
