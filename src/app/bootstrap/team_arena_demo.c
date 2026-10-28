/* Port of src/app/bootstrap/team-arena-demo.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/team_arena_demo.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/input/support.h"
void qa_app_team_arena_demo_free(qa_app_team_arena_demo *d) { free(d->name); free(d->path); d->name = NULL; d->path = NULL; }
static bool map_ok(const char *map) {
  if (map[0] == '\0') return false;
  for (const char *p = map; *p; p++) {
    char c = *p;
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '/' || c == '-')) return false;
  }
  /* no empty or ".." path part */
  const char *part = map;
  for (const char *p = map;; p++) {
    if (*p == '/' || *p == '\0') {
      size_t len = (size_t)(p - part);
      if (len == 0 || (len == 2 && part[0] == '.' && part[1] == '.')) return false;
      if (*p == '\0') break;
      part = p + 1;
    }
  }
  return true;
}
bool qa_app_team_arena_demo_lookup(const char *map, double game_type, double protocol,
                                   bool (*exists)(void *ctx, const char *path), void *ctx,
                                   qa_app_team_arena_demo *out) {
  if (!map_ok(map) || game_type != trunc(game_type) || game_type < 0 || protocol != trunc(protocol) || protocol < 0)
    qa_range_error("Invalid Team Arena demo selection");
  char name[1024], path[1100];
  snprintf(name, sizeof name, "%s_%.0f", map, game_type);
  snprintf(path, sizeof path, "demos/%s.dm_%.0f", name, protocol);
  if (!exists(ctx, path)) return false;
  out->name = qa_input_dup(name);
  out->path = qa_input_dup(path);
  if (out->name == NULL || out->path == NULL) qa_range_error("Out of memory");
  return true;
}
