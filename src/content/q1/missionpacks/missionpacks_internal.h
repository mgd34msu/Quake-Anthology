/* Slice-internal C accommodations for q1/missionpacks (+world): string
 * comparison, Math.fround, String(number) field writes, TS property
 * string assignment, and the trace request literal the TS writes inline.
 * Everything else calls the foundation (qa_q1_*), the base and the shared
 * world tables of game->host directly. GPL-2.0-or-later. */
#ifndef QA_Q1MP_MISSIONPACKS_INTERNAL_H
#define QA_Q1MP_MISSIONPACKS_INTERNAL_H

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/missionpacks/types.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"

#define QA_Q1MP_V3(x, y, z) ((qa_vec3){(x), (y), (z)})
#define QA_Q1MP_PI 3.141592653589793 /* Math.PI (M_PI is absent under -std=c17) */

static inline bool qa_q1mp_streq(const char *a, const char *b) {
  return a != NULL && b != NULL && strcmp(a, b) == 0;
}
static inline bool qa_q1mp_starts(const char *s, const char *prefix) {
  return s != NULL && prefix != NULL && strncmp(s, prefix, strlen(prefix)) == 0;
}
/* Math.fround. */
static inline double qa_q1mp_fround(double v) { return (double)(float)v; }
/* entity.fields.set(key, String(value)). */
static inline void qa_q1mp_field_number(qa_q1_actor *entity, const char *key, double value) {
  char buffer[40];
  qa_net_js_number(value, buffer, sizeof buffer);
  if (!qa_q1_actor_field_set(entity, key, buffer)) qa_range_error("Out of memory");
}
static inline void qa_q1mp_field_text(qa_q1_actor *entity, const char *key, const char *value) {
  if (!qa_q1_actor_field_set(entity, key, value)) qa_range_error("Out of memory");
}
/* TS `entity.model = value` (and the other string properties). */
static inline void qa_q1mp_set_string(char **slot, const char *value) {
  const size_t length = strlen(value != NULL ? value : "") + 1;
  char *copy = malloc(length);
  if (copy == NULL) qa_range_error("Out of memory");
  memcpy(copy, value != NULL ? value : "", length);
  free(*slot);
  *slot = copy;
}
/* game.host.trace({ start, end, bounds: POINT, ignore, monsters }). */
static inline qa_q1_trace qa_q1mp_trace(qa_q1_services *game, qa_vec3 start, qa_vec3 end,
                                        const qa_actor_id *ignore, bool monsters) {
  qa_q1_trace_request request;
  memset(&request, 0, sizeof request);
  request.start = start; request.end = end; request.bounds = QA_Q1_POINT;
  request.has_ignore = ignore != NULL;
  if (ignore != NULL) request.ignore = *ignore;
  request.monsters = monsters;
  return game->host.trace(game->host.ctx, request);
}
/* game.find(targetname)[0]; NULL = TS undefined. */
static inline qa_q1_actor *qa_q1mp_find_first(qa_q1_services *game, const char *targetname) {
  qa_q1_actor *found[1];
  return qa_q1_find(game, targetname, found, 1) != 0 ? found[0] : NULL;
}
/* [...game.entities.values()]: a snapshot the caller frees. */
static inline qa_q1_actor **qa_q1mp_entities(qa_q1_services *game, size_t *out_count) {
  qa_q1_actor **copy = malloc((game->entity_count != 0 ? game->entity_count : 1) * sizeof *copy);
  if (copy == NULL) qa_range_error("Out of memory");
  if (game->entity_count != 0) memcpy(copy, game->entities, game->entity_count * sizeof *copy);
  *out_count = game->entity_count;
  return copy;
}
/* `for (const entity of game.entities.values())` while the body may remove
 * or create entities: JS Map iteration visits, in insertion order, every
 * entry still present when reached, including entries added during the
 * walk. game->entities keeps insertion order (splice on release, append on
 * create), so the next entry is the first present one not yet visited.
 * Initialize with {0}; free the walk with qa_q1mp_entity_walk_free. */
typedef struct qa_q1mp_entity_walk { const qa_q1_actor **visited; size_t count, capacity; } qa_q1mp_entity_walk;
static inline qa_q1_actor *qa_q1mp_entity_walk_next(qa_q1_services *game, qa_q1mp_entity_walk *walk) {
  for (size_t i = 0; i < game->entity_count; i++) {
    qa_q1_actor *entity = game->entities[i];
    bool seen = false;
    for (size_t v = 0; v < walk->count && !seen; v++) seen = walk->visited[v] == entity;
    if (seen) continue;
    if (walk->count == walk->capacity) {
      const size_t capacity = walk->capacity != 0 ? walk->capacity * 2 : 64;
      const qa_q1_actor **grown = realloc(walk->visited, capacity * sizeof *grown);
      if (grown == NULL) qa_range_error("Out of memory");
      walk->visited = grown; walk->capacity = capacity;
    }
    walk->visited[walk->count++] = entity;
    return entity;
  }
  return NULL;
}
static inline void qa_q1mp_entity_walk_free(qa_q1mp_entity_walk *walk) { free(walk->visited); walk->visited = NULL; }
/* game.options.teamplay ?? 0 */
static inline double qa_q1mp_teamplay(const qa_q1_services *game) {
  return qa_q1_option_teamplay(game);
}

#endif
