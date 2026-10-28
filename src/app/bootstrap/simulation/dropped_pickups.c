/* Port of src/app/bootstrap/simulation/dropped-pickups.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/dropped_pickups.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
typedef struct cur { qa_actor_id actor; qa_app_sim_dropped_pickup_cargo cargo; } cur;
struct qa_app_sim_source_pickup_cargo {
  qa_app_sim_pickup_registry reg; qa_provider_id provider;
  cur *current; size_t count, cap;
  qa_app_sim_dropped_pickup_level *levels; size_t level_count;
};
static char *dup(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p == NULL) qa_range_error("Out of memory"); memcpy(p, s, n); return p; }
qa_app_sim_source_pickup_cargo *qa_app_sim_source_pickup_cargo_create(qa_app_sim_pickup_registry registry, qa_provider_id provider) {
  qa_app_sim_source_pickup_cargo *c = calloc(1, sizeof *c);
  if (c == NULL) qa_range_error("Out of memory");
  c->reg = registry; c->provider = provider; return c;
}
void qa_app_sim_source_pickup_cargo_destroy(qa_app_sim_source_pickup_cargo *c) {
  if (c == NULL) return;
  qa_app_sim_dropped_pickup_levels_destroy(c->levels, c->level_count);
  free(c->current); free(c);
}
static size_t idx_of(qa_app_sim_source_pickup_cargo *c, qa_actor_id actor, bool *found) {
  for (size_t i = 0; i < c->count; i++) if (qa_same_actor(c->current[i].actor, actor)) { *found = true; return i; }
  *found = false; return 0;
}
bool qa_app_sim_source_pickup_cargo_get(qa_app_sim_source_pickup_cargo *c, qa_actor_id actor, qa_app_sim_dropped_pickup_cargo *out) {
  bool f; size_t i = idx_of(c, actor, &f); if (!f) return false; *out = c->current[i].cargo; return true;
}
void qa_app_sim_source_pickup_cargo_set(qa_app_sim_source_pickup_cargo *c, qa_actor_id actor, qa_app_sim_dropped_pickup_cargo cargo) {
  bool safe = cargo.count == trunc(cargo.count) && fabs(cargo.count) <= 9007199254740991.0;
  if (!c->reg.is_live(c->reg.ctx, actor) || !safe || cargo.count <= 0) qa_range_error("Invalid original dropped pickup cargo");
  bool f; size_t i = idx_of(c, actor, &f);
  if (f) { c->current[i].cargo = cargo; return; }
  if (c->count == c->cap) { c->cap = c->cap != 0 ? c->cap * 2 : 8; c->current = realloc(c->current, c->cap * sizeof *c->current); if (c->current == NULL) qa_range_error("Out of memory"); }
  c->current[c->count].actor = actor; c->current[c->count].cargo = cargo; c->count++;
}
void qa_app_sim_source_pickup_cargo_delete(qa_app_sim_source_pickup_cargo *c, qa_actor_id actor) {
  bool f; size_t i = idx_of(c, actor, &f); if (!f) return;
  memmove(&c->current[i], &c->current[i + 1], (c->count - i - 1) * sizeof *c->current); c->count--;
}
void qa_app_sim_source_pickup_cargo_release(qa_app_sim_source_pickup_cargo *c, qa_actor_id actor) { qa_app_sim_source_pickup_cargo_delete(c, actor); }
void qa_app_sim_dropped_pickup_levels_destroy(qa_app_sim_dropped_pickup_level *levels, size_t count) {
  if (levels == NULL) return;
  for (size_t i = 0; i < count; i++) { free((void *)levels[i].map); free(levels[i].items); }
  free(levels);
}
static qa_app_sim_dropped_pickup_level *copy_levels(const qa_app_sim_dropped_pickup_level *src, size_t n) {
  qa_app_sim_dropped_pickup_level *out = calloc(n != 0 ? n : 1, sizeof *out);
  if (out == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < n; i++) {
    out[i].map = dup(src[i].map); out[i].item_count = src[i].item_count;
    out[i].items = calloc(src[i].item_count != 0 ? src[i].item_count : 1, sizeof *out[i].items);
    if (out[i].items == NULL) qa_range_error("Out of memory");
    memcpy(out[i].items, src[i].items, src[i].item_count * sizeof *out[i].items);
  }
  return out;
}
qa_app_sim_dropped_pickup_level *qa_app_sim_source_pickup_cargo_travel(qa_app_sim_source_pickup_cargo *c, const char *map, bool new_unit, size_t *out_count) {
  size_t n = new_unit ? 0 : c->level_count;
  qa_app_sim_dropped_pickup_level *levels = new_unit ? calloc(1, sizeof *levels) : copy_levels(c->levels, c->level_count);
  if (levels == NULL) qa_range_error("Out of memory");
  if (!new_unit) {
    qa_app_sim_dropped_pickup_slot *items = calloc(c->count != 0 ? c->count : 1, sizeof *items);
    if (items == NULL) qa_range_error("Out of memory");
    for (size_t i = 0; i < c->count; i++) {
      qa_provider_id prov = NULL; double slot = 0;
      if (!c->reg.source_of(c->reg.ctx, c->current[i].actor, &prov, &slot) || prov == NULL || c->provider == NULL || strcmp(prov, c->provider) != 0)
        qa_range_error("Dropped pickup has no original level allocation");
      items[i].item = c->current[i].cargo.item; items[i].count = c->current[i].cargo.count; items[i].slot = slot;
    }
    size_t found = n; bool has = false;
    for (size_t i = 0; i < n; i++) if (strcmp(levels[i].map, map) == 0) { found = i; has = true; }
    if (has) { free(levels[found].items); levels[found].items = items; levels[found].item_count = c->count; }
    else {
      levels = realloc(levels, (n + 1) * sizeof *levels); if (levels == NULL) qa_range_error("Out of memory");
      levels[n].map = dup(map); levels[n].items = items; levels[n].item_count = c->count; n++;
    }
  }
  *out_count = n; return levels;
}
void qa_app_sim_source_pickup_cargo_revisit(qa_app_sim_source_pickup_cargo *c, const qa_app_sim_dropped_pickup_level *levels, size_t count, const char *map) {
  qa_app_sim_dropped_pickup_levels_destroy(c->levels, c->level_count);
  c->levels = copy_levels(levels, count); c->level_count = count;
  for (size_t i = 0; i < count; i++) if (strcmp(levels[i].map, map) == 0) {
    for (size_t j = 0; j < levels[i].item_count; j++) {
      qa_actor_id actor;
      if (!c->reg.at_source(c->reg.ctx, c->provider, levels[i].items[j].slot, &actor))
        qa_range_error("Retained dropped pickup has no restored source allocation");
      qa_app_sim_dropped_pickup_cargo cargo = {levels[i].items[j].item, levels[i].items[j].count};
      qa_app_sim_source_pickup_cargo_set(c, actor, cargo);
    }
  }
}
