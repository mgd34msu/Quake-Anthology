/*
 * Port of src/content/roots.ts: game-root discovery by anchor
 * directory, Steam library folders, Nightdive add-on roots, and the
 * default content-root search. C accommodations:
 *  - GameRoots (a ReadonlyMap) is a fixed 3-slot record indexed by
 *    qa_game_family; string results are malloc'd (NULL is the TS
 *    null, and also OOM where noted).
 *  - String-list results are malloc'd arrays of malloc'd strings;
 *    free with qa_content_free_string_list.
 *  - ownDirectory() uses the working directory (the run-from-source
 *    branch; Bun.main has no counterpart). homedir() is $HOME.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_ROOTS_H
#define QA_CONTENT_ROOTS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/content.h"

const char *qa_game_family_directory(qa_game_family family);
/* False is the TS null. */
bool qa_game_family_for_directory(const char *name, qa_game_family *out);
/* Malloc'd; NULL only on OOM. */
char *qa_game_relative_path(const char *canonical_path, qa_game_family family);

/* Malloc'd game root, or NULL (TS null / OOM). */
char *qa_game_root_in(const char *root, qa_game_family family);

typedef struct qa_game_roots {
  char *roots[3]; /* indexed by qa_game_family; NULL = TS null */
} qa_game_roots;
qa_game_roots qa_resolve_game_roots(const char *root);
void qa_game_roots_free(qa_game_roots *roots);

bool qa_has_game_anchor(const char *root);

void qa_content_free_string_list(char **list, size_t count);
/* `home` NULL means homedir(). False only on OOM. */
bool qa_steam_common_directories(const char *home, char ***out, size_t *out_count);

typedef struct qa_nightdive_addon_root {
  qa_game_family family;
  char *root; /* owned */
} qa_nightdive_addon_root;
bool qa_nightdive_addon_directories(const char *home,
                                    qa_nightdive_addon_root **out,
                                    size_t *out_count);
void qa_nightdive_addon_roots_free(qa_nightdive_addon_root *roots, size_t count);

typedef struct qa_content_root_search {
  const char *own_directory;  /* optional (NULL = default) */
  const char *home_directory; /* optional (NULL = homedir) */
} qa_content_root_search;

bool qa_content_root_candidates(qa_content_root_search search, bool steam_only,
                                char ***out, size_t *out_count);
/* Malloc'd; NULL only on OOM. */
char *qa_resolve_default_content_root(qa_content_root_search search,
                                      bool steam_only);

#endif
