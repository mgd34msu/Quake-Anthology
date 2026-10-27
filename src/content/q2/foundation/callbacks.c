/* Port of src/content/q2/foundation/callbacks.ts. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/callbacks.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

void qa_q2_free_entity_fn(void *ctx, qa_q2_entity *e, qa_q2_game_services *g) {
  (void)ctx;
  g->remove(g->ctx, e);
}
const qa_q2_think QA_Q2_FREE_ENTITY = {NULL, qa_q2_free_entity_fn};
void qa_q2_free_entity_die_fn(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, qa_death_reaction r) {
  (void)ctx; (void)r;
  g->remove(g->ctx, e);
}
const qa_q2_die QA_Q2_FREE_ENTITY_DIE = {NULL, qa_q2_free_entity_die_fn};

/* One generic name<->callback map per kind; callbacks stored as fn+ctx pairs. */
typedef struct entry { const char *name; uintptr_t fn; void *ctx; } entry;

typedef struct map { entry *v; size_t n, cap; } map;
struct qa_q2_source_callbacks {
  map think, use, touch, pain, die, blocked;
  struct tr { const char *name; void *ctx; void (*project)(void *, qa_q2_entity *, qa_weapon_trajectory_update); } *tr;
  size_t tr_n, tr_cap;
};

static void map_register(map *m, const char *name, uintptr_t fn, void *ctx) {
  for (size_t i = 0; i < m->n; i++)
    if (strcmp(m->v[i].name, name) == 0) {
      if (m->v[i].fn != fn || m->v[i].ctx != ctx) qa_range_error("Duplicate Q2 source callback %s", name);
      return;
    }
  if (m->n == m->cap) {
    size_t cap = m->cap ? m->cap * 2 : 16;
    entry *next = realloc(m->v, cap * sizeof *next);
    if (!next) qa_range_error("out of memory");
    m->v = next; m->cap = cap;
  }
  m->v[m->n++] = (entry){name, fn, ctx};
}
static const char *map_name(map *m, uintptr_t fn, void *ctx) {
  for (size_t i = 0; i < m->n; i++)
    if (m->v[i].fn == fn && m->v[i].ctx == ctx) return m->v[i].name;
  qa_range_error("Q2 save encountered an unnamed source callback");
}
static entry map_resolve(map *m, const char *name) {
  for (size_t i = 0; i < m->n; i++)
    if (strcmp(m->v[i].name, name) == 0) return m->v[i];
  qa_range_error("Q2 restore cannot resolve source callback %s", name);
}

qa_q2_source_callbacks *qa_q2_source_callbacks_create(void) { return calloc(1, sizeof(qa_q2_source_callbacks)); }
void qa_q2_source_callbacks_destroy(qa_q2_source_callbacks *c) {
  if (!c) return;
  free(c->think.v); free(c->use.v); free(c->touch.v); free(c->pain.v); free(c->die.v); free(c->blocked.v); free(c->tr);
  free(c);
}

void qa_q2_source_callbacks_register(qa_q2_source_callbacks *c, qa_q2_callback_definitions d) {
  for (size_t i = 0; i < d.think_count; i++) map_register(&c->think, d.think[i].name, (uintptr_t)d.think[i].cb.fn, d.think[i].cb.ctx);
  for (size_t i = 0; i < d.use_count; i++) map_register(&c->use, d.use[i].name, (uintptr_t)d.use[i].cb.fn, d.use[i].cb.ctx);
  for (size_t i = 0; i < d.touch_count; i++) map_register(&c->touch, d.touch[i].name, (uintptr_t)d.touch[i].cb.fn, d.touch[i].cb.ctx);
  for (size_t i = 0; i < d.pain_count; i++) map_register(&c->pain, d.pain[i].name, (uintptr_t)d.pain[i].cb.fn, d.pain[i].cb.ctx);
  for (size_t i = 0; i < d.die_count; i++) map_register(&c->die, d.die[i].name, (uintptr_t)d.die[i].cb.fn, d.die[i].cb.ctx);
  for (size_t i = 0; i < d.blocked_count; i++) map_register(&c->blocked, d.blocked[i].name, (uintptr_t)d.blocked[i].cb.fn, d.blocked[i].cb.ctx);
  for (size_t i = 0; i < d.trajectory_count; i++) {
    if (d.trajectory[i].touch.fn == NULL) qa_range_error("Trajectory projection requires a named source touch callback");
    const char *name = map_name(&c->touch, (uintptr_t)d.trajectory[i].touch.fn, d.trajectory[i].touch.ctx);
    bool exists = false;
    for (size_t j = 0; j < c->tr_n; j++)
      if (strcmp(c->tr[j].name, name) == 0) {
        exists = true;
        if (c->tr[j].project != d.trajectory[i].project || c->tr[j].ctx != d.trajectory[i].ctx)
          qa_range_error("Duplicate Q2 trajectory projection %s", name);
      }
    if (!exists) {
      if (c->tr_n == c->tr_cap) {
        size_t cap = c->tr_cap ? c->tr_cap * 2 : 8;
        void *next = realloc(c->tr, cap * sizeof *c->tr);
        if (!next) qa_range_error("out of memory");
        c->tr = next; c->tr_cap = cap;
      }
      c->tr[c->tr_n].name = name; c->tr[c->tr_n].ctx = d.trajectory[i].ctx; c->tr[c->tr_n].project = d.trajectory[i].project;
      c->tr_n++;
    }
  }
}

void qa_q2_source_callbacks_project_trajectory(qa_q2_source_callbacks *c, qa_q2_entity *e, qa_weapon_trajectory_update u) {
  if (e->touch.fn == NULL) return;
  const char *name = NULL;
  for (size_t i = 0; i < c->touch.n; i++)
    if (c->touch.v[i].fn == (uintptr_t)e->touch.fn && c->touch.v[i].ctx == e->touch.ctx) name = c->touch.v[i].name;
  /* TS: touch.name() throws for unnamed non-null callbacks. */
  if (name == NULL) qa_range_error("Q2 save encountered an unnamed source callback");
  for (size_t i = 0; i < c->tr_n; i++)
    if (strcmp(c->tr[i].name, name) == 0) c->tr[i].project(c->tr[i].ctx, e, u);
}

#define KIND(M, T, S) \
const char *qa_q2_##S##_name(qa_q2_source_callbacks *c, T cb, bool has) { return has ? map_name(&c->M, (uintptr_t)cb.fn, cb.ctx) : NULL; } \
T qa_q2_##S##_resolve(qa_q2_source_callbacks *c, const char *name, bool *has) { \
  if (name == NULL) { *has = false; return (T){0}; } \
  entry en = map_resolve(&c->M, name); *has = true; \
  T out; out.ctx = en.ctx; memcpy(&out.fn, &en.fn, sizeof out.fn < sizeof en.fn ? sizeof out.fn : sizeof en.fn); return out; }
KIND(think, qa_q2_think, think)
KIND(use, qa_q2_use, use)
KIND(touch, qa_q2_touch, touch)
KIND(pain, qa_q2_pain, pain)
KIND(die, qa_q2_die, die)
KIND(blocked, qa_q2_blocked, blocked)
