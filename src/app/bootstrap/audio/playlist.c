/* Port of src/app/bootstrap/audio/playlist.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/audio/playlist.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/audio/playlist_settings.h"
#include "qa/content/roots.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
static void *xmalloc(size_t n) { void *p = malloc(n == 0 ? 1 : n); if (p == NULL) qa_range_error("Out of memory"); return p; }
static void *xrealloc(void *p, size_t n) { void *q = realloc(p, n == 0 ? 1 : n); if (q == NULL) qa_range_error("Out of memory"); return q; }
static char *xstrdup(const char *s) { char *p = strdup(s); if (p == NULL) qa_range_error("Out of memory"); return p; }
static int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
static bool contains(char *const *list, size_t count, const char *value) {
  for (size_t i = 0; i < count; i++) if (strcmp(list[i], value) == 0) return true;
  return false;
}
static size_t slash_parts(const char *value) {
  size_t parts = 1;
  for (; *value != '\0'; value++) if (*value == '/') parts++;
  return parts;
}
char **qa_app_mounted_music_tracks(qa_content_mounted *mounts, size_t *out_count) {
  char **tracks = NULL; size_t track_count = 0, track_cap = 0;
  char **directories = NULL; size_t dir_count = 0, dir_cap = 0;
  char **seen = NULL; size_t seen_count = 0, seen_cap = 0;
  directories = xmalloc(sizeof *directories); dir_cap = 1;
  directories[dir_count++] = xstrdup("music");
  for (size_t index = 0; index < dir_count && index < 4095 && track_count < 4095; index++) {
    const char *directory = directories[index];
    if (strlen(directory) >= 256 || contains(seen, seen_count, directory)) continue;
    if (seen_count == seen_cap) { seen_cap = seen_cap == 0 ? 8 : seen_cap * 2; seen = xrealloc(seen, seen_cap * sizeof *seen); }
    seen[seen_count++] = xstrdup(directory);
    static const char *const extensions[] = {".ogg", ".wav"};
    for (size_t e = 0; e < 2; e++) {
      size_t count = 0;
      char **names = qa_content_mounted_list_files(mounts, directory, extensions[e], &count);
      if (names == NULL) continue;
      for (size_t i = 0; i < count; i++) {
        if (track_count == 4095) break;
        size_t len = strlen(directory) + 1 + strlen(names[i]) + 1;
        char *path = xmalloc(len);
        snprintf(path, len, "%s/%s", directory, names[i]);
        if (qa_app_valid_menu_track(path) && !contains(tracks, track_count, path)) {
          if (track_count == track_cap) { track_cap = track_cap == 0 ? 8 : track_cap * 2; tracks = xrealloc(tracks, track_cap * sizeof *tracks); }
          tracks[track_count++] = path;
        } else free(path);
      }
      qa_content_free_string_list(names, count);
    }
    if (slash_parts(directory) < 16) {
      size_t count = 0;
      char **names = qa_content_mounted_list_files(mounts, directory, "/", &count);
      if (names != NULL) {
        for (size_t i = 0; i < count; i++) {
          if (dir_count == 4095) break;
          char *child = xstrdup(names[i]);
          size_t len = strlen(child);
          while (len > 0 && (child[len - 1] == '\\' || child[len - 1] == '/')) child[--len] = '\0';
          if (child[0] != '\0') {
            size_t need = strlen(directory) + 1 + len + 1;
            char *joined = xmalloc(need);
            snprintf(joined, need, "%s/%s", directory, child);
            if (dir_count == dir_cap) { dir_cap *= 2; directories = xrealloc(directories, dir_cap * sizeof *directories); }
            directories[dir_count++] = joined;
          }
          free(child);
        }
        qa_content_free_string_list(names, count);
      }
    }
  }
  for (size_t i = 0; i < dir_count; i++) free(directories[i]);
  free(directories);
  for (size_t i = 0; i < seen_count; i++) free(seen[i]);
  free(seen);
  if (tracks == NULL) tracks = xmalloc(sizeof *tracks);
  qa_js_sort(tracks, track_count, sizeof *tracks, cmp_str);
  *out_count = track_count;
  return tracks;
}
char *qa_app_music_file_cue(const char *path) {
  bool space = false;
  for (const char *p = path; *p != '\0'; p++) if (isspace((unsigned char)*p)) space = true;
  if (!space) return xstrdup(path);
  size_t len = strlen(path) + 3;
  char *out = xmalloc(len);
  snprintf(out, len, "\"%s\"", path);
  return out;
}
char **qa_app_shuffled_tracks(const char *const *tracks, size_t count, const char *previous,
                              double (*random)(void *ctx), void *random_ctx, size_t *out_count) {
  char **bag = xmalloc((count == 0 ? 1 : count) * sizeof *bag);
  size_t n = 0;
  for (size_t i = 0; i < count; i++)
    if (!contains(bag, n, tracks[i])) bag[n++] = xstrdup(tracks[i]);
  for (long long index = (long long)n - 1; index > 0; index--) {
    long long selected = (long long)floor(random(random_ctx) * (double)(index + 1));
    char *tmp = bag[index]; bag[index] = bag[selected]; bag[selected] = tmp;
  }
  if (n > 1) {
    char *cue = qa_app_music_file_cue(bag[0]);
    bool same = strcmp(cue, previous) == 0;
    free(cue);
    if (same) {
      char *first = bag[0];
      memmove(&bag[0], &bag[1], (n - 1) * sizeof *bag);
      bag[n - 1] = first;
    }
  }
  *out_count = n;
  return bag;
}
