/* Port of src/bots/behavior/q3/catalog.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_Q3_CATALOG_H
#define QA_BOTS_BEHAVIOR_Q3_CATALOG_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct qa_game_bot_catalog_bots_a {
  char **bot_names;
  size_t bot_count;
  void *ctx;
  const char *(*read_file)(void *ctx, const char *path);
} qa_game_bot_catalog_bots_a;
void qa_game_bot_catalog_bots_a_init(qa_game_bot_catalog_bots_a *c);
void qa_game_bot_catalog_bots_a_destroy(qa_game_bot_catalog_bots_a *c);
int64_t qa_game_bot_catalog_count(const qa_game_bot_catalog_bots_a *c);
const char *qa_game_bot_catalog_name(const qa_game_bot_catalog_bots_a *c, int64_t index);
int64_t qa_game_bot_catalog_find(const qa_game_bot_catalog_bots_a *c, const char *name);
bool qa_game_bot_catalog_add(qa_game_bot_catalog_bots_a *c, const char *name);
#endif
