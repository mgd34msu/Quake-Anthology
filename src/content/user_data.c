/* Port of src/content/user-data.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/content/user_data.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "qa/content/mounts/paths.h"

/* Lexical absolute normalization (node:path resolve). */
static char *resolve_home(const char *suffix) {
  const char *home = getenv("HOME");
  if (home == NULL) home = "";
  char joined[PATH_MAX * 2];
  if (home[0] == '/') {
    (void)snprintf(joined, sizeof joined, "%s/%s", home, suffix);
  } else {
    char cwd[PATH_MAX] = "";
    if (getcwd(cwd, sizeof cwd) == NULL) cwd[0] = '\0';
    (void)snprintf(joined, sizeof joined, "%s/%s/%s", cwd, home, suffix);
  }
  /* The joined path has no dot segments for the fixed suffixes here,
   * except ".." in digest-cache style callers (not this file). */
  size_t length = strlen(joined) + 1;
  char *out = (char *)malloc(length);
  if (out != NULL) memcpy(out, joined, length);
  return out;
}

char *qa_default_user_content_root(void) {
  return resolve_home(".local/share/quake-typescript/content");
}

bool qa_default_addon_content_roots(const char *home,
                                    qa_nightdive_addon_root **out,
                                    size_t *out_count) {
  const char *resolved = home;
  char *owned_home = NULL;
  if (resolved == NULL) {
    const char *env = getenv("HOME");
    resolved = env != NULL ? env : "";
  }
  bool ok = qa_nightdive_addon_directories(resolved, out, out_count);
  free(owned_home);
  return ok;
}

char *qa_user_product_directory(const char *user_content_root,
                                const char *content_directory) {
  return qa_path_within_root(user_content_root, content_directory);
}
