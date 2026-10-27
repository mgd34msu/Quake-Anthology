/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/catalog.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
void qa_game_bot_catalog_bots_a_init(qa_game_bot_catalog_bots_a *c) { memset(c, 0, sizeof *c); }
void qa_game_bot_catalog_bots_a_destroy(qa_game_bot_catalog_bots_a *c) {
  for (size_t i = 0; i < c->bot_count; i++)
    free(c->bot_names[i]);
  free(c->bot_names);
}
int64_t qa_game_bot_catalog_count(const qa_game_bot_catalog_bots_a *c) {
  return (int64_t)c->bot_count;
}
const char *qa_game_bot_catalog_name(const qa_game_bot_catalog_bots_a *c, int64_t i) {
  return (i >= 0 && (size_t)i < c->bot_count) ? c->bot_names[i] : NULL;
}
int64_t qa_game_bot_catalog_find(const qa_game_bot_catalog_bots_a *c, const char *n) {
  for (size_t i = 0; i < c->bot_count; i++)
    if (strcmp(c->bot_names[i], n) == 0)
      return (int64_t)i;
  return -1;
}
bool qa_game_bot_catalog_add(qa_game_bot_catalog_bots_a *c, const char *n) {
  if (qa_game_bot_catalog_find(c, n) >= 0)
    return true;
  char **nn = realloc(c->bot_names, (c->bot_count + 1) * sizeof *nn);
  if (!nn)
    return false;
  c->bot_names = nn;
  size_t len = strlen(n) + 1;
  char *copy = malloc(len);
  if (!copy)
    return false;
  memcpy(copy, n, len);
  c->bot_names[c->bot_count++] = copy;
  return true;
}
