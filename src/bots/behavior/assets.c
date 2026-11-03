/* See header. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/bots/behavior/assets.h"
#include <openssl/evp.h>
#include "qa/foundation/support.h"
#include <errno.h>
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
    free(f->files[i].legacy_bytes);
  }
  free(f->files);
  for (size_t i = 0; i < f->unread_count; i++) {
    free(f->unread[i].path);
    free(f->unread[i].key);
  }
  free(f->unread);
  if (f->owned_open_free != NULL) f->owned_open_free(f->owned_open_ctx);
  memset(f, 0, sizeof *f);
}
static const qa_bot_asset_unread *find_unread(const qa_bot_asset_files *f, const char *key) {
  for (size_t i = 0; i < f->unread_count; i++)
    if (strcmp(f->unread[i].key, key) == 0) return &f->unread[i];
  return NULL;
}
static bool set_legacy(qa_bot_asset_file *e, const uint8_t *legacy, size_t legacy_len) {
  if (legacy == NULL) return true;
  uint8_t *copy = malloc(legacy_len ? legacy_len : 1);
  if (!copy) return false;
  memcpy(copy, legacy, legacy_len);
  free(e->legacy_bytes);
  e->legacy_bytes = copy;
  e->legacy_length = legacy_len;
  e->has_legacy = true;
  return true;
}
bool qa_bot_asset_files_add(qa_bot_asset_files *f, const char *path, const uint8_t *bytes,
                            size_t len) {
  return qa_bot_asset_files_add_with_legacy(f, path, bytes, len, NULL, 0);
}
/* TS add: a path already listed (read or unread) keeps its place in `paths`. Entries live in
 * `files` in insertion order; a key also recorded unread keeps the unread entry, which the
 * digests and listing then honour, as TS `unread.has` does. */
bool qa_bot_asset_files_add_with_legacy(qa_bot_asset_files *f, const char *path, const uint8_t *bytes,
                                        size_t len, const uint8_t *legacy, size_t legacy_len) {
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
      return set_legacy(&f->files[i], legacy, legacy_len);
    }
  const qa_bot_asset_unread *unread = find_unread(f, key);
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
  memset(e, 0, sizeof *e);
  e->path = fwd_path(path);
  e->key = key;
  e->bytes = malloc(len ? len : 1);
  if (!e->bytes || !e->path)
    return false;
  memcpy(e->bytes, bytes, len);
  e->length = len;
  e->sequence = unread != NULL ? unread->sequence : f->next_sequence++;
  f->count++;
  return set_legacy(e, legacy, legacy_len);
}
bool qa_bot_asset_files_add_unread(qa_bot_asset_files *f, const char *path, qa_bot_asset_open_fn open,
                                   void *open_ctx) {
  char *key = normalize_key(path);
  if (!key) return false;
  bool listed = find_unread(f, key) != NULL;
  for (size_t i = 0; i < f->count && !listed; i++) listed = strcmp(f->files[i].key, key) == 0;
  if (listed) { free(key); return true; }
  if (f->unread_count == f->unread_capacity) {
    size_t nc = f->unread_capacity ? f->unread_capacity * 2 : 16;
    qa_bot_asset_unread *grown = realloc(f->unread, nc * sizeof *grown);
    if (!grown) { free(key); return false; }
    f->unread = grown;
    f->unread_capacity = nc;
  }
  qa_bot_asset_unread *u = &f->unread[f->unread_count];
  u->path = fwd_path(path);
  if (!u->path) { free(key); return false; }
  u->key = key;
  u->sequence = f->next_sequence++;
  u->open = open;
  u->open_ctx = open_ctx;
  f->unread_count++;
  return true;
}
static void put_u32(uint8_t out[4], uint32_t v) {
  out[0] = (uint8_t)v; out[1] = (uint8_t)(v >> 8); out[2] = (uint8_t)(v >> 16); out[3] = (uint8_t)(v >> 24);
}
/* TS hashEntry: u32le name length, u32le byte length, UTF-8 name, bytes. Paths are byte strings here. */
static void hash_entry(EVP_MD_CTX *hash, const char *path, const uint8_t *bytes, size_t length) {
  uint8_t size[8];
  put_u32(size, (uint32_t)strlen(path));
  put_u32(size + 4, (uint32_t)length);
  EVP_DigestUpdate(hash, size, 8);
  EVP_DigestUpdate(hash, path, strlen(path));
  if (length != 0) EVP_DigestUpdate(hash, bytes, length);
}
static EVP_MD_CTX *hash_begin(void) {
  EVP_MD_CTX *hash = EVP_MD_CTX_new();
  if (hash == NULL || EVP_DigestInit_ex(hash, EVP_sha256(), NULL) != 1) qa_range_error("Out of memory");
  return hash;
}
static void hash_end(EVP_MD_CTX *hash, char out[65]) {
  unsigned char digest[32];
  unsigned int length = 0;
  EVP_DigestFinal_ex(hash, digest, &length);
  EVP_MD_CTX_free(hash);
  for (unsigned int i = 0; i < 32; i++) snprintf(out + i * 2, 3, "%02x", digest[i]);
}
void qa_bot_asset_files_provenance(const qa_bot_asset_files *f, char out[65]) {
  EVP_MD_CTX *hash = hash_begin();
  for (size_t i = 0; i < f->count; i++) {
    const qa_bot_asset_file *file = &f->files[i];
    if (find_unread(f, file->key) != NULL) continue;
    if (file->bytes == NULL) {
      EVP_MD_CTX_free(hash);
      qa_raise("Error", 0, 0, "Bot asset provenance lost its mounted payload");
    }
    hash_entry(hash, file->path, file->bytes, file->length);
  }
  hash_end(hash, out);
}
bool qa_bot_asset_files_prepare_legacy_provenance(qa_bot_asset_files *f) {
  EVP_MD_CTX *hash = hash_begin();
  /* TS iterates `paths`: read and unread entries merged by their listing sequence. */
  size_t r = 0, u = 0;
  while (r < f->count || u < f->unread_count) {
    bool take_unread = r >= f->count || (u < f->unread_count && f->unread[u].sequence < f->files[r].sequence);
    if (take_unread) {
      const qa_bot_asset_unread *entry = &f->unread[u++];
      const uint8_t *bytes = NULL;
      size_t length = 0;
      /* A navigation file that does not exist was never part of the version 1 digest either. */
      if (!entry->open(entry->open_ctx, entry->path, &bytes, &length)) continue;
      hash_entry(hash, entry->path, bytes, length);
      continue;
    }
    const qa_bot_asset_file *file = &f->files[r++];
    const qa_bot_asset_unread *shadow = find_unread(f, file->key);
    if (shadow != NULL) continue; /* the unread entry stands for this path at its own sequence */
    hash_entry(hash, file->path, file->has_legacy ? file->legacy_bytes : file->bytes,
               file->has_legacy ? file->legacy_length : file->length);
  }
  hash_end(hash, f->legacy_digest);
  f->has_legacy_digest = true;
  return true;
}
const char *qa_bot_asset_files_legacy_provenance(const qa_bot_asset_files *f) {
  if (!f->has_legacy_digest) qa_raise("Error", 0, 0, "Version 1 bot saves require prepared legacy asset provenance");
  return f->legacy_digest;
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
    if (find_unread(f, key) != NULL)
      continue;
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
/* TS NAVIGATION_PATH /^bots\/navigation\//i over the path with backslashes replaced. */
static bool navigation_path(const char *path) {
  static const char prefix[] = "bots/navigation/";
  for (size_t i = 0; i < sizeof prefix - 1; i++) {
    char c = path[i] == '\\' ? '/' : path[i];
    if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    if (c != prefix[i]) return false;
  }
  return true;
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
      bool dup = find_unread(out, key) != NULL;
      for (size_t j = 0; j < out->count; j++)
        if (strcmp(out->files[j].key, key) == 0)
          dup = true;
      bool slash = kl && key[kl - 1] == '/';
      free(key);
      if (dup || slash)
        continue;
      if (navigation_path(path)) {
        /* Navigation graphs are read through the mounted resources by the navigation loader, never through these files. */
        if (!qa_bot_asset_files_add_unread(out, path, open_fn, ctx))
          return false;
        continue;
      }
      const uint8_t *bytes = NULL;
      size_t blen = 0;
      if (open_fn(ctx, path, &bytes, &blen))
        if (!qa_bot_asset_files_add(out, path, bytes, blen))
          return false;
    }
  }
  return true;
}

/* ---- loadMountedBotAssetFiles(mounts, catalog, extraPaths) ---- */
#include <dirent.h>
#include <sys/stat.h>
#include "qa/content/catalog/index.h"
#include "qa/content/mounts.h"
#include "qa/foundation/support.h"
typedef struct mounted_paths { char **items; size_t count, capacity; } mounted_paths;
static void mounted_paths_push(mounted_paths *paths, char *path) {
  if (path == NULL) qa_range_error("Out of memory");
  if (paths->count == paths->capacity) {
    size_t next = paths->capacity == 0 ? 64 : paths->capacity * 2;
    char **items = realloc(paths->items, next * sizeof *items);
    if (items == NULL) qa_range_error("Out of memory");
    paths->items = items; paths->capacity = next;
  }
  paths->items[paths->count++] = path;
}
static char *join_path(const char *a, const char *b) {
  size_t na = strlen(a), nb = strlen(b);
  char *out = malloc(na + nb + 2);
  if (out == NULL) qa_range_error("Out of memory");
  memcpy(out, a, na); out[na] = '/'; memcpy(out + na + 1, b, nb + 1);
  return out;
}
static int compare_names(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
/* TS loose(root, directory): readdir(resolve(root, directory)) sorted by name; a missing
 * directory lists nothing; directories recurse, regular files are listed (Dirent kinds: a
 * symbolic link is neither). Other readdir failures throw. */
static void list_loose(const char *root, const char *directory, mounted_paths *paths) {
  char *full = join_path(root, directory);
  DIR *dir = opendir(full);
  if (dir == NULL) {
    int error = errno;
    free(full);
    if (error == ENOENT) return;
    qa_raise("Error", 0, 0, "%s: %s", directory, strerror(error));
  }
  char **names = NULL; size_t count = 0, capacity = 0;
  for (struct dirent *entry = readdir(dir); entry != NULL; entry = readdir(dir)) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
    if (count == capacity) {
      capacity = capacity == 0 ? 16 : capacity * 2;
      char **next = realloc(names, capacity * sizeof *names);
      if (next == NULL) qa_range_error("Out of memory");
      names = next;
    }
    size_t length = strlen(entry->d_name);
    names[count] = malloc(length + 1);
    if (names[count] == NULL) qa_range_error("Out of memory");
    memcpy(names[count++], entry->d_name, length + 1);
  }
  closedir(dir);
  qsort(names, count, sizeof *names, compare_names); /* names are distinct, so order is total */
  for (size_t i = 0; i < count; i++) {
    char *path = join_path(directory, names[i]);
    char *entry_path = join_path(full, names[i]);
    struct stat info;
    bool known = lstat(entry_path, &info) == 0;
    free(entry_path);
    if (known && S_ISDIR(info.st_mode)) { list_loose(root, path, paths); free(path); }
    else if (known && S_ISREG(info.st_mode)) mounted_paths_push(paths, path);
    else free(path);
    free(names[i]);
  }
  free(names);
  free(full);
}
static bool starts_with(const char *text, const char *prefix) { return strncmp(text, prefix, strlen(prefix)) == 0; }
static const qa_catalog_archive *catalog_archive(const qa_content_catalog *catalog, const char *path) {
  /* TS new Map([...rootArchives, ...products.flatMap(archives)]): a later entry with the same path wins. */
  for (size_t p = catalog->product_count; p-- > 0;)
    for (size_t a = catalog->products[p].archive_count; a-- > 0;)
      if (strcmp(catalog->products[p].archives[a].path, path) == 0) return &catalog->products[p].archives[a];
  for (size_t a = catalog->root_archive_count; a-- > 0;)
    if (strcmp(catalog->root_archives[a].path, path) == 0) return &catalog->root_archives[a];
  return NULL;
}
typedef struct mounted_open { qa_content_mounted *mounts; uint8_t *bytes; } mounted_open;
static bool mounted_open_path(void *ctx, const char *path, const uint8_t **out_bytes, size_t *out_len) {
  mounted_open *open = ctx;
  free(open->bytes);
  open->bytes = NULL;
  qa_content_opened_resource opened;
  memset(&opened, 0, sizeof opened);
  if (!qa_content_mounted_open(open->mounts, path, NULL, NULL, &opened)) return false;
  open->bytes = opened.bytes;
  *out_bytes = opened.bytes;
  *out_len = opened.byte_length;
  return true;
}
static void mounted_open_free(void *ctx) {
  mounted_open *open = ctx;
  free(open->bytes);
  free(open);
}
bool qa_load_mounted_bot_asset_files(qa_bot_asset_files *out, qa_content_mounted *mounts, const qa_content_catalog *catalog,
                                     const char *const *extra_paths, size_t extra_count) {
  const qa_resolved_mount_plan *plan = qa_content_mounted_plan(mounts);
  mounted_paths paths = {0};
  static const char *const directories[] = {"botfiles", "bots", "scripts"};
  for (size_t d = 0; d < 3; d++) {
    const char *directory = directories[d];
    char *slashed = join_path(directory, "");
    const qa_mount_id *order = plan->default_order;
    size_t order_count = plan->default_order_count;
    for (size_t i = 0; i < plan->prefix_order_count; i++) {
      char *prefix = normalize_key(plan->prefix_orders[i].prefix); /* TS entry.prefix.toLowerCase() */
      if (prefix == NULL) qa_range_error("Out of memory");
      bool match = starts_with(slashed, prefix);
      free(prefix);
      if (match) { order = plan->prefix_orders[i].mounts; order_count = plan->prefix_orders[i].mount_count; break; }
    }
    for (size_t o = 0; o < order_count; o++) {
      const qa_content_mount *mount = NULL;
      for (size_t m = 0; m < plan->mount_count && mount == NULL; m++) {
        const qa_mount_identity *identity = plan->mounts[m].kind == QA_CONTENT_MOUNT_LOOSE
            ? &plan->mounts[m].as.loose.identity : &plan->mounts[m].as.archive.identity;
        if (strcmp(identity->id, order[o]) == 0) mount = &plan->mounts[m];
      }
      if (mount == NULL) qa_raise("Error", 0, 0, "Bot listing references missing mount %s", order[o]);
      if (mount->kind == QA_CONTENT_MOUNT_LOOSE) { list_loose(mount->as.loose.root_path, directory, &paths); continue; }
      const qa_catalog_archive *archive = catalog_archive(catalog, mount->as.archive.archive_path);
      if (archive == NULL) qa_raise("Error", 0, 0, "Bot listing lacks mounted archive index %s", mount->as.archive.archive_path);
      for (size_t e = 0; e < archive->entry_count; e++) {
        char *key = normalize_key(archive->entries[e].path);
        if (key == NULL) qa_range_error("Out of memory");
        bool listed = starts_with(key, slashed);
        free(key);
        if (!listed) continue;
        size_t length = strlen(archive->entries[e].path);
        char *path = malloc(length + 1);
        if (path != NULL) memcpy(path, archive->entries[e].path, length + 1);
        mounted_paths_push(&paths, path);
      }
    }
    free(slashed);
  }
  /* The opener outlives this call: unread navigation entries reopen through it for a legacy digest. */
  mounted_open *open = malloc(sizeof *open);
  if (open == NULL) qa_range_error("Out of memory");
  open->mounts = mounts;
  open->bytes = NULL;
  bool ok = qa_load_bot_asset_files(out, mounted_open_path, open, (const char *const *)paths.items, paths.count,
                                    extra_paths, extra_count);
  out->owned_open_ctx = open;
  out->owned_open_free = mounted_open_free;
  for (size_t i = 0; i < paths.count; i++) free(paths.items[i]);
  free(paths.items);
  return ok;
}
