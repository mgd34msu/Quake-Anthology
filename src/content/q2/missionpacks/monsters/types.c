/* Port of src/content/q2/missionpacks/monsters/types.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/types.h"

#include <stddef.h>
#include <stdlib.h>

const char *qa_q2mpm_pack_name(qa_q2mpm_pack pack) {
  return pack == QA_Q2MPM_PACK_XATRIX ? "xatrix" : "rogue";
}

void *qa_q2mpm_arena_alloc(qa_q2mpm_arena *arena, size_t size) {
  if (arena == NULL) return malloc(size ? size : 1);
  if (arena->count == arena->cap) {
    size_t cap = arena->cap ? arena->cap * 2 : 32;
    void **grown = realloc(arena->items, cap * sizeof *grown);
    if (!grown) return NULL;
    arena->items = grown;
    arena->cap = cap;
  }
  void *item = malloc(size ? size : 1);
  if (item) arena->items[arena->count++] = item;
  return item;
}
void qa_q2mpm_arena_destroy(qa_q2mpm_arena *arena) {
  for (size_t i = 0; i < arena->count; i++) free(arena->items[i]);
  free(arena->items);
  arena->items = NULL;
  arena->count = arena->cap = 0;
}
