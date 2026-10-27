/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char *normalize_key(const char *path) {
  const char *p = path;
  if (p[0] == '.' && p[1] == '/')
    p += 2;
  size_t n = strlen(p);
  char *k = malloc(n + 1);
  if (!k)
    return NULL;
  for (size_t i = 0; i < n; i++) {
    char c = p[i] == '\\' ? '/' : p[i];
    if (c >= 'A' && c <= 'Z')
      c = (char)(c + 32);
    k[i] = c;
  }
  k[n] = '\0';
  return k;
}
static char *fwd_path(const char *path) {
  size_t n = strlen(path);
  char *k = malloc(n + 1);
  if (!k)
    return NULL;
  for (size_t i = 0; i < n; i++)
    k[i] = path[i] == '\\' ? '/' : path[i];
  k[n] = '\0';
  return k;
}
void qa_bot_asset_files_init(qa_bot_asset_files *f) { memset(f, 0, sizeof *f); }
void qa_bot_asset_files_destroy(qa_bot_asset_files *f) {
  for (size_t i = 0; i < f->count; i++) {
    free(f->files[i].path);
    free(f->files[i].key);
    free(f->files[i].bytes);
  }
  free(f->files);
}
bool qa_bot_asset_files_add(qa_bot_asset_files *f, const char *path, const uint8_t *bytes,
                            size_t len) {
  char *key = normalize_key(path);
  if (!key)
    return false;
  for (size_t i = 0; i < f->count; i++)
    if (strcmp(f->files[i].key, key) == 0) {
      free(f->files[i].bytes);
      f->files[i].bytes = malloc(len ? len : 1);
      if (!f->files[i].bytes) {
        free(key);
        return false;
      }
      memcpy(f->files[i].bytes, bytes, len);
      f->files[i].length = len;
      free(key);
      return true;
    }
  if (f->count == f->capacity) {
    size_t nc = f->capacity ? f->capacity * 2 : 16;
    qa_bot_asset_file *nf = realloc(f->files, nc * sizeof *nf);
    if (!nf) {
      free(key);
      return false;
    }
    f->files = nf;
    f->capacity = nc;
  }
  qa_bot_asset_file *e = &f->files[f->count];
  e->path = fwd_path(path);
  e->key = key;
  e->bytes = malloc(len ? len : 1);
  if (!e->bytes || !e->path)
    return false;
  memcpy(e->bytes, bytes, len);
  e->length = len;
  f->count++;
  return true;
}
int64_t qa_bot_asset_files_read(qa_bot_asset_files *f, const char *path, uint8_t *out, size_t cap) {
  char *key = normalize_key(path);
  if (!key)
    return -1;
  for (size_t i = 0; i < f->count; i++)
    if (strcmp(f->files[i].key, key) == 0) {
      size_t n = f->files[i].length;
      if (out)
        memcpy(out, f->files[i].bytes, n < cap ? n : cap);
      free(key);
      return (int64_t)n;
    }
  free(key);
  return -1;
}
size_t qa_bot_asset_files_list(qa_bot_asset_files *f, const char *dir, const char *ext,
                               const char **out_names, size_t max_names) {
  char *dk = normalize_key(dir);
  if (!dk)
    return 0;
  size_t dl = strlen(dk);
  while (dl && dk[dl - 1] == '/')
    dk[--dl] = '\0';
  char prefix[1024];
  snprintf(prefix, sizeof prefix, "%s/", dk);
  free(dk);
  size_t plen = strlen(prefix), elen = strlen(ext), count = 0, bytes_used = 0;
  char lower_ext[256];
  for (size_t i = 0; i < elen && i < 255; i++) {
    char c = ext[i];
    lower_ext[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
  }
  lower_ext[elen < 255 ? elen : 255] = '\0';
  for (size_t i = 0; i < f->count; i++) {
    const char *key = f->files[i].key;
    size_t kl = strlen(key);
    if (strncmp(key, prefix, plen) != 0)
      continue;
    if (kl < elen || strcmp(key + kl - elen, lower_ext) != 0)
      continue;
    const char *name = f->files[i].path + plen;
    if (strchr(name, '/'))
      continue;
    if (bytes_used + strlen(name) + 1 >= 1024)
      break;
    if (count < max_names)
      out_names[count] = name;
    count++;
    bytes_used += strlen(name) + 1;
  }
  return count;
}
static bool candidate_ok(const char *p) {
  /* ^(botfiles|bots)/ or ^scripts/.*\.(txt|bot|arena)$ case-insensitive */
  char low[1024];
  size_t n = strlen(p);
  if (n > 1023)
    n = 1023;
  for (size_t i = 0; i < n; i++) {
    char c = p[i];
    low[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
  }
  low[n] = '\0';
  if (strncmp(low, "botfiles/", 9) == 0 || strncmp(low, "bots/", 5) == 0)
    return true;
  if (strncmp(low, "scripts/", 8) == 0) {
    size_t l = strlen(low);
    return (l >= 4 && strcmp(low + l - 4, ".txt") == 0) ||
           (l >= 4 && strcmp(low + l - 4, ".bot") == 0) ||
           (l >= 6 && strcmp(low + l - 6, ".arena") == 0);
  }
  return false;
}
bool qa_load_bot_asset_files(qa_bot_asset_files *out, qa_bot_asset_open_fn open_fn, void *ctx,
                             const char *const *ordered, size_t on, const char *const *extra,
                             size_t en) {
  qa_bot_asset_files_init(out);
  /* dedup by normalized key via read check on out plus loaded set approximated by out keys +
   * skipped */
  for (int pass = 0; pass < 2; pass++) {
    const char *const *list = pass == 0 ? ordered : extra;
    size_t ln = pass == 0 ? on : en;
    for (size_t i = 0; i < ln; i++) {
      const char *path = list[i];
      if (pass == 0 && !candidate_ok(path))
        continue;
      char *key = normalize_key(path);
      if (!key)
        return false;
      size_t kl = strlen(key);
      bool dup = false;
      for (size_t j = 0; j < out->count; j++)
        if (strcmp(out->files[j].key, key) == 0)
          dup = true;
      bool slash = kl && key[kl - 1] == '/';
      free(key);
      if (dup || slash)
        continue;
      const uint8_t *bytes = NULL;
      size_t blen = 0;
      if (open_fn(ctx, path, &bytes, &blen))
        if (!qa_bot_asset_files_add(out, path, bytes, blen))
          return false;
    }
  }
  return true;
}
