/* Port of src/content/mounts/index.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include "qa/content/mounts.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "qa/content/archive/loose.h"
#include "qa/content/archive/source.h"
#include "qa/content/roots.h"
#include "qa/content/user_data.h"
#include "qa/core/md4.h"
#include "qa/foundation/support.h"
#include "qa/persistence/value.h"

static noreturn void plain_error(const char *format, const char *argument) {
  fprintf(stderr, "Error: ");
  fprintf(stderr, format, argument);
  fputc('\n', stderr);
  abort();
}
static noreturn void oom_error(void) { qa_range_error("out of memory"); }
static char *copy_text(const char *text) {
  if (text == NULL) return NULL;
  size_t length = strlen(text) + 1;
  char *copy = (char *)malloc(length);
  if (copy != NULL) memcpy(copy, text, length);
  return copy;
}
static char *copy_text_or_oom(const char *text) {
  char *copy = copy_text(text);
  if (copy == NULL && text != NULL) oom_error();
  return copy;
}
static bool text_equal(const char *left, const char *right) {
  if (left == NULL || right == NULL) return left == right;
  return strcmp(left, right) == 0;
}
static char *lowered_copy(const char *text) {
  char *out = copy_text(text);
  if (out == NULL) return NULL;
  for (size_t i = 0; out[i] != '\0'; i++)
    if (out[i] >= 'A' && out[i] <= 'Z') out[i] = (char)(out[i] - 'A' + 'a');
  return out;
}
static bool starts_with(const char *text, const char *prefix) {
  return strncmp(text, prefix, strlen(prefix)) == 0;
}
static bool starts_with_ci(const char *text, const char *prefix) {
  char *lower_text = lowered_copy(text);
  char *lower_prefix = lowered_copy(prefix);
  bool result = lower_text != NULL && lower_prefix != NULL &&
                starts_with(lower_text, lower_prefix);
  free(lower_text);
  free(lower_prefix);
  return result;
}

/* Lexical absolute normalization (node:path resolve / dirname). */
static char *normalize_absolute(const char *joined) {
  size_t length = strlen(joined);
  char *copy = copy_text(joined);
  if (copy == NULL) return NULL;
  char **parts = (char **)malloc((length / 2 + 2) * sizeof *parts);
  if (parts == NULL) { free(copy); return NULL; }
  size_t count = 0;
  char *cursor = copy;
  while (*cursor != '\0') {
    while (*cursor == '/') cursor++;
    if (*cursor == '\0') break;
    char *start = cursor;
    while (*cursor != '\0' && *cursor != '/') cursor++;
    size_t part_length = (size_t)(cursor - start);
    if (*cursor == '/') *cursor++ = '\0';
    if (part_length == 1 && start[0] == '.') continue;
    if (part_length == 2 && start[0] == '.' && start[1] == '.') {
      if (count > 0) count--;
      continue;
    }
    parts[count++] = start;
  }
  char *out = (char *)malloc(length + 2);
  if (out == NULL) { free(parts); free(copy); return NULL; }
  size_t at = 0;
  if (count == 0) out[at++] = '/';
  for (size_t i = 0; i < count; i++) {
    out[at++] = '/';
    size_t part_length = strlen(parts[i]);
    memcpy(out + at, parts[i], part_length);
    at += part_length;
  }
  out[at] = '\0';
  free(parts);
  free(copy);
  return out;
}
static char *resolve2(const char *first, const char *second) {
  char joined[PATH_MAX * 2];
  if (second != NULL && second[0] == '/') {
    (void)snprintf(joined, sizeof joined, "%s", second);
  } else if (first[0] == '/') {
    if (second != NULL) (void)snprintf(joined, sizeof joined, "%s/%s", first, second);
    else (void)snprintf(joined, sizeof joined, "%s", first);
  } else {
    char cwd[PATH_MAX] = "";
    if (getcwd(cwd, sizeof cwd) == NULL) cwd[0] = '\0';
    if (second != NULL)
      (void)snprintf(joined, sizeof joined, "%s/%s/%s", cwd, first, second);
    else
      (void)snprintf(joined, sizeof joined, "%s/%s", cwd, first);
  }
  return normalize_absolute(joined);
}
static char *relative_path(const char *from, const char *to) {
  char *resolved_from = resolve2(from, NULL);
  char *resolved_to = resolve2(to, NULL);
  if (resolved_from == NULL || resolved_to == NULL) {
    free(resolved_from); free(resolved_to);
    return NULL;
  }
  if (strcmp(resolved_from, resolved_to) == 0) {
    free(resolved_from); free(resolved_to);
    return copy_text("");
  }
  size_t from_length = strlen(resolved_from);
  char *result = NULL;
  if (strcmp(resolved_from, "/") != 0 &&
      strncmp(resolved_to, resolved_from, from_length) == 0 &&
      resolved_to[from_length] == '/') {
    result = copy_text(resolved_to + from_length + 1);
  } else if (strcmp(resolved_from, "/") == 0 && resolved_to[0] == '/') {
    result = copy_text(resolved_to + 1);
  } else {
    /* Generic ".." walk: count from-segments past the common prefix. */
    size_t common = 0;
    while (resolved_from[common] != '\0' && resolved_to[common] != '\0' &&
           resolved_from[common] == resolved_to[common])
      common++;
    while (common > 0 && resolved_to[common] != '/') common--;
    size_t ups = 0;
    for (const char *p = resolved_from + common; *p != '\0'; p++)
      if (*p == '/') ups++;
    size_t rest_length = strlen(resolved_to + common + (resolved_to[common] == '/' ? 1 : 0));
    result = (char *)malloc(ups * 3 + rest_length + 1);
    if (result != NULL) {
      size_t at = 0;
      for (size_t i = 0; i < ups; i++) {
        memcpy(result + at, "../", 3);
        at += 3;
      }
      const char *rest = resolved_to + common + (resolved_to[common] == '/' ? 1 : 0);
      memcpy(result + at, rest, rest_length + 1);
      if (at > 0 && result[at - 1] == '/' && rest_length == 0) result[at - 1] = '\0';
    }
  }
  free(resolved_from);
  free(resolved_to);
  return result;
}

/* ---- digests ---------------------------------------------------- */

static void sha256_hex(const uint8_t *data, size_t length, char out[65]) {
  uint8_t digest[SHA256_DIGEST_LENGTH];
  SHA256(data, length, digest);
  static const char hexdigits[] = "0123456789abcdef";
  for (size_t i = 0; i < SHA256_DIGEST_LENGTH; i++) {
    out[i * 2] = hexdigits[digest[i] >> 4];
    out[i * 2 + 1] = hexdigits[digest[i] & 0x0f];
  }
  out[64] = '\0';
}

char *qa_digest_bytes(const uint8_t *bytes, size_t length) {
  char hex[65];
  sha256_hex(bytes, length, hex);
  return qa_create_content_digest(hex);
}

typedef struct digest_fingerprint {
  double size;
  double mtime_ms;
  double ctime_ms;
  double ino;
  char *digest; /* owned */
} digest_fingerprint;
typedef struct digest_entry {
  char *path; /* owned */
  digest_fingerprint value;
} digest_entry;

static digest_entry *verified_digests = NULL;
static size_t verified_count = 0, verified_capacity = 0;
static digest_entry *disk_digests = NULL;
static size_t disk_count = 0;
static bool disk_loaded = false;

static const digest_fingerprint *digest_lookup(const digest_entry *entries,
                                               size_t count, const char *path) {
  for (size_t i = 0; i < count; i++)
    if (strcmp(entries[i].path, path) == 0) return &entries[i].value;
  return NULL;
}
static void digest_store(digest_entry **entries, size_t *count,
                         size_t *capacity, const char *path,
                         digest_fingerprint value) {
  for (size_t i = 0; i < *count; i++) {
    if (strcmp((*entries)[i].path, path) == 0) {
      free((*entries)[i].value.digest);
      (*entries)[i].value = value;
      return;
    }
  }
  if (*count == *capacity) {
    size_t next = *capacity == 0 ? 8 : *capacity * 2;
    digest_entry *grown =
        (digest_entry *)realloc(*entries, next * sizeof *grown);
    if (grown == NULL) { free(value.digest); return; }
    *entries = grown;
    *capacity = next;
  }
  char *copy = copy_text(path);
  if (copy == NULL) { free(value.digest); return; }
  (*entries)[*count].path = copy;
  (*entries)[*count].value = value;
  (*count)++;
}
static bool fingerprint_matches(const digest_fingerprint *entry,
                                const digest_fingerprint *want) {
  return entry != NULL && entry->size == want->size &&
         entry->mtime_ms == want->mtime_ms &&
         entry->ctime_ms == want->ctime_ms && entry->ino == want->ino;
}

static char *digest_cache_file(void) {
  char *root = qa_default_user_content_root();
  if (root == NULL) return NULL;
  char joined[PATH_MAX * 2];
  (void)snprintf(joined, sizeof joined, "%s/../cache/archive-digests.json", root);
  free(root);
  return normalize_absolute(joined);
}

static void load_disk_digests(void) {
  if (disk_loaded) return;
  disk_loaded = true;
  char *file = digest_cache_file();
  if (file == NULL) return;
  FILE *handle = fopen(file, "rb");
  free(file);
  if (handle == NULL) return;
  size_t capacity = 4096, length = 0;
  uint8_t *buffer = (uint8_t *)malloc(capacity);
  if (buffer == NULL) { (void)fclose(handle); return; }
  for (;;) {
    if (length == capacity) {
      capacity *= 2;
      uint8_t *grown = (uint8_t *)realloc(buffer, capacity);
      if (grown == NULL) { free(buffer); (void)fclose(handle); return; }
      buffer = grown;
    }
    size_t read = fread(buffer + length, 1, capacity - length, handle);
    length += read;
    if (read == 0) break;
  }
  (void)fclose(handle);
  qa_save_value *parsed = qa_parse_json_value(buffer, length, true);
  free(buffer);
  if (parsed == NULL || parsed->kind != QA_SAVE_OBJECT) {
    qa_save_value_free(parsed);
    return;
  }
  size_t disk_capacity = 0;
  for (size_t i = 0; i < parsed->entry_count; i++) {
    const qa_save_value *value = parsed->entries[i].value;
    if (value == NULL || value->kind != QA_SAVE_OBJECT) continue;
    const qa_save_value *size = qa_save_value_get(value, "size");
    const qa_save_value *mtime = qa_save_value_get(value, "mtimeMs");
    const qa_save_value *ctime = qa_save_value_get(value, "ctimeMs");
    const qa_save_value *ino = qa_save_value_get(value, "ino");
    const qa_save_value *digest = qa_save_value_get(value, "digest");
    if (size == NULL || size->kind != QA_SAVE_NUMBER || mtime == NULL ||
        mtime->kind != QA_SAVE_NUMBER || ctime == NULL ||
        ctime->kind != QA_SAVE_NUMBER || ino == NULL ||
        ino->kind != QA_SAVE_NUMBER || digest == NULL ||
        digest->kind != QA_SAVE_STRING)
      continue;
    digest_fingerprint entry_value = {size->number, mtime->number,
                                      ctime->number, ino->number,
                                      copy_text(digest->text)};
    if (entry_value.digest != NULL)
      digest_store(&disk_digests, &disk_count, &disk_capacity,
                   parsed->entries[i].key, entry_value);
  }
  qa_save_value_free(parsed);
}

static void json_escape_into(FILE *file, const char *text) {
  fputc('"', file);
  for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; p++) {
    if (*p == '"' || *p == '\\') fprintf(file, "\\%c", *p);
    else if (*p < 0x20) fprintf(file, "\\u%04x", *p);
    else fputc(*p, file);
  }
  fputc('"', file);
}
static void mkdir_parents(const char *path) {
  char buffer[PATH_MAX];
  (void)snprintf(buffer, sizeof buffer, "%s", path);
  for (char *p = buffer + 1; *p != '\0'; p++) {
    if (*p != '/') continue;
    *p = '\0';
    (void)mkdir(buffer, 0777);
    *p = '/';
  }
}

/* The TS saves on a 250 ms unref'd timer; the cache is best-effort. */
static void save_disk_digests(void) {
  load_disk_digests();
  size_t capacity = disk_count;
  for (size_t i = 0; i < verified_count; i++) {
    digest_fingerprint copy = verified_digests[i].value;
    copy.digest = copy_text(verified_digests[i].value.digest);
    if (copy.digest != NULL)
      digest_store(&disk_digests, &disk_count, &capacity,
                   verified_digests[i].path, copy);
  }
  char *file = digest_cache_file();
  if (file == NULL) return;
  char *directory = copy_text(file);
  if (directory != NULL) {
    char *slash = strrchr(directory, '/');
    if (slash != NULL) *slash = '\0';
    mkdir_parents(directory);
    free(directory);
  }
  char temporary[PATH_MAX];
  (void)snprintf(temporary, sizeof temporary, "%s.tmp", file);
  FILE *handle = fopen(temporary, "wb");
  if (handle == NULL) { free(file); return; }
  fputc('{', handle);
  for (size_t i = 0; i < disk_count; i++) {
    if (i > 0) fputc(',', handle);
    json_escape_into(handle, disk_digests[i].path);
    const digest_fingerprint *value = &disk_digests[i].value;
    fprintf(handle,
            ":{\"size\":%.17g,\"mtimeMs\":%.17g,\"ctimeMs\":%.17g,\"ino\":%.17g,\"digest\":",
            value->size, value->mtime_ms, value->ctime_ms, value->ino);
    json_escape_into(handle, value->digest);
    fputc('}', handle);
  }
  fputc('}', handle);
  bool ok = fclose(handle) == 0;
  if (ok) (void)rename(temporary, file);
  free(file);
}

static double timespec_ms(struct timespec value) {
  return (double)value.tv_sec * 1000.0 + (double)value.tv_nsec / 1000000.0;
}

char *qa_digest_file(const char *path) {
  struct stat info;
  if (stat(path, &info) != 0) plain_error("%s", strerror(errno));
  digest_fingerprint fingerprint = {(double)info.st_size,
                                    timespec_ms(info.st_mtim),
                                    timespec_ms(info.st_ctim),
                                    (double)info.st_ino, NULL};
  bool cache_enabled = getenv("QTS_NO_DIGEST_CACHE") == NULL;
  if (cache_enabled) {
    const digest_fingerprint *memory =
        digest_lookup(verified_digests, verified_count, path);
    if (fingerprint_matches(memory, &fingerprint))
      return qa_create_content_digest(memory->digest);
    load_disk_digests();
    const digest_fingerprint *disk =
        digest_lookup(disk_digests, disk_count, path);
    if (fingerprint_matches(disk, &fingerprint)) {
      digest_fingerprint copy = *disk;
      copy.digest = copy_text(disk->digest);
      if (copy.digest != NULL)
        digest_store(&verified_digests, &verified_count, &verified_capacity,
                     path, copy);
      return qa_create_content_digest(disk->digest);
    }
  }
  FILE *file = fopen(path, "rb");
  if (file == NULL) plain_error("%s", strerror(errno));
  EVP_MD_CTX *context = EVP_MD_CTX_new();
  if (context == NULL) { (void)fclose(file); return NULL; }
  (void)EVP_DigestInit_ex(context, EVP_sha256(), NULL);
  uint8_t *buffer = (uint8_t *)malloc(4u * 1024u * 1024u);
  if (buffer == NULL) { EVP_MD_CTX_free(context); (void)fclose(file); return NULL; }
  for (;;) {
    size_t read = fread(buffer, 1, 4u * 1024u * 1024u, file);
    if (read == 0) break;
    (void)EVP_DigestUpdate(context, buffer, read);
  }
  free(buffer);
  (void)fclose(file);
  uint8_t raw_digest[SHA256_DIGEST_LENGTH];
  unsigned int raw_length = 0;
  (void)EVP_DigestFinal_ex(context, raw_digest, &raw_length);
  EVP_MD_CTX_free(context);
  char hex[65];
  static const char hexdigits[] = "0123456789abcdef";
  for (size_t i = 0; i < SHA256_DIGEST_LENGTH; i++) {
    hex[i * 2] = hexdigits[raw_digest[i] >> 4];
    hex[i * 2 + 1] = hexdigits[raw_digest[i] & 0x0f];
  }
  hex[64] = '\0';
  if (cache_enabled) {
    fingerprint.digest = copy_text(hex);
    if (fingerprint.digest != NULL) {
      digest_store(&verified_digests, &verified_count, &verified_capacity,
                   path, fingerprint);
      save_disk_digests();
    }
  }
  return qa_create_content_digest(hex);
}

/* ---- mount plans ------------------------------------------------ */

typedef struct mounted_source {
  qa_content_mount mount; /* by value; strings borrowed from the plan */
  qa_archive_handle *archive; /* NULL for loose */
} mounted_source;

typedef struct opened_record {
  int64_t key;
  qa_resolved_resource_reference reference; /* strings owned, below */
  char *requested_path;
  char *member_path;
  char *digest;
  char *id;
  char *prefix;
  char *source_prefix;
  char *target_path;
} opened_record;

typedef struct listing_entry {
  char *name; /* owned */
  unsigned char type; /* DT_* */
} listing_entry;
typedef struct listing_cache {
  char *directory; /* owned */
  bool missing;
  listing_entry *entries;
  size_t count;
} listing_cache;

struct qa_content_mounted {
  qa_resolved_mount_plan plan;
  qa_content_mount *plan_mounts;         /* owned array */
  qa_mount_id *plan_default_order;       /* owned array */
  qa_prefix_mount_order *plan_prefixes;  /* owned array */
  qa_mount_id **plan_prefix_mounts;      /* owned arrays */
  qa_open_mount_options options;
  qa_content_digest *pure_archives; /* owned array of borrowed strings */
  qa_resource_link *links;          /* owned array of borrowed strings */
  mounted_source *sources;
  size_t source_count;
  qa_mount_id *user_mounts; /* owned array of borrowed strings */
  size_t user_mount_count, user_mount_capacity;
  qa_archive_mount *referenced; /* by value */
  size_t referenced_count, referenced_capacity;
  opened_record *opened;
  size_t opened_count, opened_capacity;
  qa_resolved_resource_reference *opened_view; /* scratch for the getter */
  /* Opened-key map: (mount id, requested path) -> key. */
  struct opened_key_entry {
    qa_mount_id mount;
    char *path; /* owned */
    int64_t key;
  } *opened_keys;
  size_t opened_key_count, opened_key_capacity;
  int64_t next_opened_key;
  listing_cache *listings;
  size_t listing_count, listing_capacity;
  bool closed;
  /* TS #ownership: owned, or borrowed (parent assertOpen). */
  enum { OWNER_NONE, OWNER_MOUNTED, OWNER_SCOPE } owner_kind;
  void *owner; /* qa_content_mounted* or qa_mount_preparation_scope* */
};

static const qa_mount_identity *mount_identity(const qa_content_mount *mount) {
  return mount->kind == QA_CONTENT_MOUNT_ARCHIVE ? &mount->as.archive.identity
                                                 : &mount->as.loose.identity;
}
static bool same_mount(const qa_content_mount *left,
                       const qa_content_mount *right) {
  const qa_mount_identity *li = mount_identity(left);
  const qa_mount_identity *ri = mount_identity(right);
  if (!text_equal(li->id, ri->id) || !text_equal(li->content, ri->content) ||
      li->generation != ri->generation)
    return false;
  if (left->kind == QA_CONTENT_MOUNT_ARCHIVE)
    return right->kind == QA_CONTENT_MOUNT_ARCHIVE &&
           text_equal(left->as.archive.archive_path,
                      right->as.archive.archive_path) &&
           text_equal(left->as.archive.archive_digest,
                      right->as.archive.archive_digest) &&
           left->as.archive.format == right->as.archive.format;
  return right->kind == QA_CONTENT_MOUNT_LOOSE &&
         text_equal(left->as.loose.root_path, right->as.loose.root_path);
}
static const qa_content_mount *find_mount(const qa_content_mount *mounts,
                                          size_t count, qa_mount_id id) {
  for (size_t i = 0; i < count; i++)
    if (text_equal(mount_identity(&mounts[i])->id, id)) return &mounts[i];
  return NULL;
}
static void validate_order(const qa_mount_id *order, size_t order_count,
                           const qa_content_mount *mounts, size_t mount_count) {
  for (size_t i = 0; i < order_count; i++) {
    if (find_mount(mounts, mount_count, order[i]) == NULL)
      qa_range_error("Mount order refers to unknown mount: %s", order[i]);
    for (size_t j = 0; j < i; j++)
      if (text_equal(order[j], order[i]))
        qa_range_error("Mount order repeats mount: %s", order[i]);
  }
  if (order_count != mount_count)
    qa_range_error("Mount order must include every fallback mount");
}
static qa_mount_id *pure_order(const qa_mount_id *order, size_t count,
                               const qa_content_mount *mounts,
                               size_t mount_count,
                               const qa_open_mount_options *options) {
  qa_mount_id *remaining =
      (qa_mount_id *)malloc((count ? count : 1) * sizeof *remaining);
  if (remaining == NULL) oom_error();
  if (count > 0) memcpy(remaining, order, count * sizeof *remaining);
  if (options == NULL || !options->has_pure || options->pure.archive_count == 0)
    return remaining;
  qa_mount_id *out = (qa_mount_id *)malloc((count ? count : 1) * sizeof *out);
  if (out == NULL) oom_error();
  size_t remaining_count = count, first_count = 0;
  for (size_t d = 0; d < options->pure.archive_count; d++) {
    for (size_t i = 0; i < remaining_count; i++) {
      const qa_content_mount *mount =
          find_mount(mounts, mount_count, remaining[i]);
      if (mount != NULL && mount->kind == QA_CONTENT_MOUNT_ARCHIVE &&
          text_equal(mount->as.archive.archive_digest,
                     options->pure.archives[d])) {
        out[first_count++] = remaining[i];
        memmove(&remaining[i], &remaining[i + 1],
                (remaining_count - i - 1) * sizeof *remaining);
        remaining_count--;
        break;
      }
    }
  }
  if (remaining_count > 0)
    memcpy(&out[first_count], remaining, remaining_count * sizeof *out);
  free(remaining);
  return out;
}
static bool pure_loose_path(const char *path) {
  static const char *const extensions[] = {".cfg", ".menu", ".game",
                                           ".dm_68", ".dat"};
  size_t length = strlen(path);
  for (size_t i = 0; i < sizeof extensions / sizeof extensions[0]; i++) {
    size_t extension_length = strlen(extensions[i]);
    if (length < extension_length) continue;
    bool equal = true;
    for (size_t j = 0; j < extension_length; j++) {
      char a = path[length - extension_length + j];
      if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
      if (a != extensions[i][j]) equal = false;
    }
    if (equal) return true;
  }
  return false;
}
static char *strip_trailing_slash(const char *value) {
  size_t length = strlen(value);
  if (length > 0 && value[length - 1] == '/') length--;
  char *out = (char *)malloc(length + 1);
  if (out == NULL) oom_error();
  memcpy(out, value, length);
  out[length] = '\0';
  return out;
}
static void validate_prefix_path(const char *prefix) {
  char *stripped = strip_trailing_slash(prefix);
  char *normalized = qa_normalize_resource_path(stripped);
  free(stripped);
  if (normalized == NULL) oom_error();
  free(normalized);
}
static void resolve_order_into(qa_content_mounted *mounted,
                               const qa_resolved_mount_plan *plan,
                               const qa_open_mount_options *options) {
  validate_order(plan->default_order, plan->default_order_count, plan->mounts,
                 plan->mount_count);
  for (size_t i = 0; i < plan->prefix_order_count; i++) {
    validate_prefix_path(plan->prefix_orders[i].prefix);
    validate_order(plan->prefix_orders[i].mounts,
                   plan->prefix_orders[i].mount_count, plan->mounts,
                   plan->mount_count);
  }
  mounted->plan_mounts = (qa_content_mount *)malloc(
      (plan->mount_count ? plan->mount_count : 1) * sizeof *mounted->plan_mounts);
  if (mounted->plan_mounts == NULL) oom_error();
  if (plan->mount_count > 0)
    memcpy(mounted->plan_mounts, plan->mounts,
           plan->mount_count * sizeof *mounted->plan_mounts);
  mounted->plan_default_order =
      pure_order(plan->default_order, plan->default_order_count, plan->mounts,
                 plan->mount_count, options);
  mounted->plan_prefixes = (qa_prefix_mount_order *)calloc(
      plan->prefix_order_count ? plan->prefix_order_count : 1,
      sizeof *mounted->plan_prefixes);
  mounted->plan_prefix_mounts = (qa_mount_id **)calloc(
      plan->prefix_order_count ? plan->prefix_order_count : 1,
      sizeof *mounted->plan_prefix_mounts);
  if (mounted->plan_prefixes == NULL || mounted->plan_prefix_mounts == NULL)
    oom_error();
  for (size_t i = 0; i < plan->prefix_order_count; i++) {
    mounted->plan_prefix_mounts[i] =
        pure_order(plan->prefix_orders[i].mounts,
                   plan->prefix_orders[i].mount_count, plan->mounts,
                   plan->mount_count, options);
    mounted->plan_prefixes[i].prefix = plan->prefix_orders[i].prefix;
    mounted->plan_prefixes[i].mounts = mounted->plan_prefix_mounts[i];
    mounted->plan_prefixes[i].mount_count = plan->prefix_orders[i].mount_count;
  }
  mounted->plan.id = plan->id;
  mounted->plan.mounts = mounted->plan_mounts;
  mounted->plan.mount_count = plan->mount_count;
  mounted->plan.default_order = mounted->plan_default_order;
  mounted->plan.default_order_count = plan->default_order_count;
  mounted->plan.prefix_orders = mounted->plan_prefixes;
  mounted->plan.prefix_order_count = plan->prefix_order_count;
}
static qa_open_mount_options empty_options(void) {
  qa_open_mount_options options;
  memset(&options, 0, sizeof options);
  options.loose_comparison = QA_CONTENT_PATH_CASE_INSENSITIVE;
  return options;
}
static void resolve_mount_plan_into(qa_content_mounted *mounted,
                                    const qa_resolved_mount_plan *plan,
                                    const qa_open_mount_options *options) {
  for (size_t i = 0; i < plan->mount_count; i++)
    for (size_t j = i + 1; j < plan->mount_count; j++)
      if (text_equal(mount_identity(&plan->mounts[i])->id,
                     mount_identity(&plan->mounts[j])->id))
        qa_range_error("Repeated mount identity: %s",
                       mount_identity(&plan->mounts[i])->id);
  resolve_order_into(mounted, plan, options);
  mounted->options = options != NULL ? *options : empty_options();
  if (options != NULL && options->has_pure) {
    mounted->pure_archives = (qa_content_digest *)malloc(
        (options->pure.archive_count ? options->pure.archive_count : 1) *
        sizeof *mounted->pure_archives);
    if (mounted->pure_archives == NULL) oom_error();
    if (options->pure.archive_count > 0)
      memcpy(mounted->pure_archives, options->pure.archives,
             options->pure.archive_count * sizeof *mounted->pure_archives);
    mounted->options.pure.archives = mounted->pure_archives;
  }
  if (options != NULL && options->link_count > 0) {
    mounted->links =
        (qa_resource_link *)malloc(options->link_count * sizeof *mounted->links);
    if (mounted->links == NULL) oom_error();
    memcpy(mounted->links, options->links,
           options->link_count * sizeof *mounted->links);
    mounted->options.links = mounted->links;
  }
  for (size_t i = 0; i < mounted->options.link_count; i++) {
    const qa_resource_link *link = &mounted->options.links[i];
    const qa_content_mount *mount =
        find_mount(plan->mounts, plan->mount_count, link->mount);
    if (mount == NULL || mount->kind != QA_CONTENT_MOUNT_LOOSE)
      qa_range_error("Link must target a mounted loose root: %s", link->mount);
    validate_prefix_path(link->source_prefix);
    if (link->target_prefix != NULL && link->target_prefix[0] != '\0')
      validate_prefix_path(link->target_prefix);
  }
  if (options != NULL && options->has_pure) {
    for (size_t d = 0; d < options->pure.archive_count; d++) {
      bool available = false;
      for (size_t i = 0; i < plan->mount_count; i++)
        if (plan->mounts[i].kind == QA_CONTENT_MOUNT_ARCHIVE &&
            text_equal(plan->mounts[i].as.archive.archive_digest,
                       options->pure.archives[d]))
          available = true;
      if (!available)
        plain_error("Required pure archive is missing: %s",
                    options->pure.archives[d]);
    }
  }
}
static qa_content_mounted *mounted_create_plan(
    const qa_resolved_mount_plan *plan, const qa_open_mount_options *options) {
  qa_content_mounted *mounted =
      (qa_content_mounted *)calloc(1, sizeof *mounted);
  if (mounted == NULL) return NULL;
  mounted->next_opened_key = 1;
  resolve_mount_plan_into(mounted, plan, options);
  return mounted;
}
static void free_opened_record(opened_record *record) {
  free(record->requested_path);
  free(record->member_path);
  free(record->digest);
  free(record->id);
  free(record->prefix);
  free(record->source_prefix);
  free(record->target_path);
  memset(record, 0, sizeof *record);
}
static void clear_listings(qa_content_mounted *mounted) {
  for (size_t i = 0; i < mounted->listing_count; i++) {
    free(mounted->listings[i].directory);
    for (size_t j = 0; j < mounted->listings[i].count; j++)
      free(mounted->listings[i].entries[j].name);
    free(mounted->listings[i].entries);
  }
  free(mounted->listings);
  mounted->listings = NULL;
  mounted->listing_count = 0;
  mounted->listing_capacity = 0;
}
const qa_resolved_mount_plan *qa_content_mounted_plan(
    const qa_content_mounted *mounted) {
  return &mounted->plan;
}
const qa_open_mount_options *qa_content_mounted_options(
    const qa_content_mounted *mounted) {
  return &mounted->options;
}
const qa_resolved_resource_reference *qa_content_mounted_opened_resources(
    const qa_content_mounted *mounted, size_t *out_count) {
  qa_content_mounted *mutable = (qa_content_mounted *)(const void *)mounted;
  free(mutable->opened_view);
  mutable->opened_view = (qa_resolved_resource_reference *)malloc(
      (mounted->opened_count ? mounted->opened_count : 1) *
      sizeof *mutable->opened_view);
  if (mutable->opened_view == NULL) oom_error();
  for (size_t i = 0; i < mounted->opened_count; i++)
    mutable->opened_view[i] = mounted->opened[i].reference;
  if (out_count != NULL) *out_count = mounted->opened_count;
  return mutable->opened_view;
}
const qa_archive_mount *qa_content_mounted_referenced_archives(
    const qa_content_mounted *mounted, size_t *out_count) {
  if (out_count != NULL) *out_count = mounted->referenced_count;
  return mounted->referenced;
}

void qa_content_mounted_assert_open(const qa_content_mounted *mounted) {
  if (mounted->closed) plain_error("%s", "Content mount plan is closed");
  if (mounted->owner_kind == 1 /* OWNER_MOUNTED */)
    qa_content_mounted_assert_open((const qa_content_mounted *)mounted->owner);
  else if (mounted->owner_kind == 2 /* OWNER_SCOPE */)
    qa_mount_preparation_scope_assert_open(
        (const qa_mount_preparation_scope *)mounted->owner);
}

static mounted_source *source_for(qa_content_mounted *mounted, qa_mount_id id) {
  for (size_t i = 0; i < mounted->source_count; i++)
    if (text_equal(mount_identity(&mounted->sources[i].mount)->id, id))
      return &mounted->sources[i];
  return NULL;
}
static bool is_user_mount(const qa_content_mounted *mounted, qa_mount_id id) {
  for (size_t i = 0; i < mounted->user_mount_count; i++)
    if (text_equal(mounted->user_mounts[i], id)) return true;
  return false;
}
static void add_user_mount(qa_content_mounted *mounted, qa_mount_id id) {
  if (is_user_mount(mounted, id)) return;
  if (mounted->user_mount_count == mounted->user_mount_capacity) {
    size_t next = mounted->user_mount_capacity == 0
                      ? 4
                      : mounted->user_mount_capacity * 2;
    qa_mount_id *grown =
        (qa_mount_id *)realloc(mounted->user_mounts, next * sizeof *grown);
    if (grown == NULL) oom_error();
    mounted->user_mounts = grown;
    mounted->user_mount_capacity = next;
  }
  mounted->user_mounts[mounted->user_mount_count++] = id;
}
static qa_content_path_comparison loose_comparison_of(
    const qa_content_mounted *mounted) {
  return mounted->options.has_loose_comparison
             ? mounted->options.loose_comparison
             : QA_CONTENT_PATH_CASE_INSENSITIVE;
}
static bool source_allowed(const qa_content_mounted *mounted,
                           const mounted_source *source, const char *path) {
  if (source->mount.kind == QA_CONTENT_MOUNT_LOOSE &&
      is_user_mount(mounted, mount_identity(&source->mount)->id))
    return true;
  if (source->mount.kind == QA_CONTENT_MOUNT_LOOSE &&
      mounted->options.q3_restriction_demo && !pure_loose_path(path))
    return false;
  if (!mounted->options.has_pure || mounted->options.pure.archive_count == 0)
    return true;
  if (source->mount.kind == QA_CONTENT_MOUNT_ARCHIVE) {
    for (size_t i = 0; i < mounted->options.pure.archive_count; i++)
      if (text_equal(mounted->options.pure.archives[i],
                     source->mount.as.archive.archive_digest))
        return true;
    return false;
  }
  return pure_loose_path(path);
}

typedef struct source_read {
  uint8_t *bytes;
  size_t byte_length;
  qa_resource_provenance provenance;
  char *member_path; /* owned scratch; provenance points elsewhere */
} source_read;

static bool read_source(qa_content_mounted *mounted, mounted_source *source,
                        const char *member_path, source_read *out) {
  memset(out, 0, sizeof *out);
  if (!source_allowed(mounted, source, member_path)) return false;
  if (source->mount.kind == QA_CONTENT_MOUNT_ARCHIVE) {
    size_t total = qa_archive_find_entries(
        source->archive, member_path, QA_ARCHIVE_PATH_CASE_INSENSITIVE, NULL, 0);
    const qa_archive_entry **found = (const qa_archive_entry **)malloc(
        (total ? total : 1) * sizeof *found);
    if (found == NULL) oom_error();
    (void)qa_archive_find_entries(source->archive, member_path,
                                  QA_ARCHIVE_PATH_CASE_INSENSITIVE, found,
                                  total);
    const qa_archive_entry *entry = NULL;
    size_t usable = 0;
    for (size_t i = 0; i < total; i++) {
      if (found[i]->is_directory) continue;
      usable++;
      if (source->mount.as.archive.format == QA_ARCHIVE_FORMAT_PAK) {
        if (entry == NULL) entry = found[i];
      } else {
        entry = found[i]; /* ZIP index replaces duplicate names: last */
      }
    }
    free(found);
    if (usable == 0 || entry == NULL) return false;
    size_t length = 0;
    uint8_t *bytes =
        qa_archive_read_entry_entry(source->archive, entry, &length);
    if (bytes == NULL && length != 0) oom_error();
    out->bytes = bytes;
    out->byte_length = length;
    out->provenance.kind = QA_CONTENT_MOUNT_ARCHIVE;
    out->provenance.archive_mount = &source->mount.as.archive;
    out->provenance.member_path = entry->path;
    out->provenance.member_index = entry->ordinal;
    out->member_path = copy_text_or_oom(entry->path);
    return true;
  }
  char *found_path = NULL;
  if (!qa_find_content_path(source->mount.as.loose.root_path, member_path,
                            loose_comparison_of(mounted), &found_path))
    return false;
  struct stat info;
  bool is_file = stat(found_path, &info) == 0 && S_ISREG(info.st_mode);
  if (!is_file) {
    free(found_path);
    return false;
  }
  char *actual = relative_path(source->mount.as.loose.root_path, found_path);
  free(found_path);
  if (actual == NULL) oom_error();
  for (size_t i = 0; actual[i] != '\0'; i++)
    if (actual[i] == '\\') actual[i] = '/';
  size_t length = 0;
  uint8_t *bytes = qa_read_loose_entry(source->mount.as.loose.root_path,
                                       actual, &length);
  if (bytes == NULL && length != 0) { free(actual); oom_error(); }
  out->bytes = bytes;
  out->byte_length = length;
  out->provenance.kind = QA_CONTENT_MOUNT_LOOSE;
  out->provenance.loose_mount = &source->mount.as.loose;
  out->provenance.member_path = actual;
  out->member_path = actual;
  return true;
}

static int64_t opened_key(qa_content_mounted *mounted, qa_mount_id mount,
                          const char *requested_path) {
  for (size_t i = 0; i < mounted->opened_key_count; i++)
    if (text_equal(mounted->opened_keys[i].mount, mount) &&
        strcmp(mounted->opened_keys[i].path, requested_path) == 0)
      return mounted->opened_keys[i].key;
  if (mounted->opened_key_count == mounted->opened_key_capacity) {
    size_t next = mounted->opened_key_capacity == 0
                      ? 8
                      : mounted->opened_key_capacity * 2;
    void *grown = realloc(mounted->opened_keys, next * sizeof *mounted->opened_keys);
    if (grown == NULL) oom_error();
    mounted->opened_keys = (struct opened_key_entry *)grown;
    mounted->opened_key_capacity = next;
  }
  int64_t key = mounted->next_opened_key++;
  mounted->opened_keys[mounted->opened_key_count].mount = mount;
  mounted->opened_keys[mounted->opened_key_count].path =
      copy_text_or_oom(requested_path);
  mounted->opened_keys[mounted->opened_key_count].key = key;
  mounted->opened_key_count++;
  return key;
}

static opened_record *store_opened(qa_content_mounted *mounted,
                                   opened_record **records_ptr, size_t *count,
                                   size_t *capacity, const char *requested_path,
                                   source_read *read,
                                   qa_resource_resolution resolution) {
  opened_record *records = *records_ptr;
  qa_resolved_resource_reference reference;
  memset(&reference, 0, sizeof reference);
  reference.requested_path = requested_path;
  reference.provenance = read->provenance;
  reference.provenance.member_path = read->member_path;
  reference.byte_length = (int64_t)read->byte_length;
  reference.resolution = resolution;
  char *digest = qa_digest_bytes(read->bytes, read->byte_length);
  if (digest == NULL) oom_error();
  reference.digest = digest;
  if (read->provenance.kind == QA_CONTENT_MOUNT_ARCHIVE) {
    qa_mount_id id = read->provenance.archive_mount->identity.id;
    bool seen = false;
    for (size_t i = 0; i < mounted->referenced_count; i++)
      if (text_equal(mounted->referenced[i].identity.id, id)) seen = true;
    if (!seen) {
      if (mounted->referenced_count == mounted->referenced_capacity) {
        size_t next = mounted->referenced_capacity == 0
                          ? 4
                          : mounted->referenced_capacity * 2;
        qa_archive_mount *grown = (qa_archive_mount *)realloc(
            mounted->referenced, next * sizeof *grown);
        if (grown == NULL) oom_error();
        mounted->referenced = grown;
        mounted->referenced_capacity = next;
      }
      mounted->referenced[mounted->referenced_count++] =
          *read->provenance.archive_mount;
    }
  }
  char *id = qa_create_resource_id(&reference);
  if (id == NULL) oom_error();
  qa_mount_id mount_id = read->provenance.kind == QA_CONTENT_MOUNT_ARCHIVE
                             ? read->provenance.archive_mount->identity.id
                             : read->provenance.loose_mount->identity.id;
  int64_t key = opened_key(mounted, mount_id, requested_path);
  opened_record *record = NULL;
  for (size_t i = 0; i < *count; i++)
    if (records[i].key == key) record = &records[i];
  if (record == NULL) {
    if (*count == *capacity) {
      size_t next = *capacity == 0 ? 8 : *capacity * 2;
      opened_record *grown =
          (opened_record *)realloc(records, next * sizeof *grown);
      if (grown == NULL) oom_error();
      records = grown;
      *records_ptr = grown;
      *capacity = next;
    }
    record = &records[*count];
    memset(record, 0, sizeof *record);
    record->key = key;
    (*count)++;
  } else {
    int64_t keep_key = record->key;
    free_opened_record(record);
    record->key = keep_key;
  }
  record->requested_path = copy_text_or_oom(requested_path);
  record->member_path = read->member_path;
  read->member_path = NULL;
  record->digest = digest;
  record->id = id;
  record->prefix = copy_text(resolution.prefix);
  record->source_prefix = copy_text(resolution.source_prefix);
  record->target_path = copy_text(resolution.target_path);
  reference.id = record->id;
  reference.requested_path = record->requested_path;
  reference.digest = record->digest;
  reference.provenance.member_path = record->member_path;
  reference.resolution.prefix = record->prefix;
  reference.resolution.source_prefix = record->source_prefix;
  reference.resolution.target_path = record->target_path;
  record->reference = reference;
  return record;
}

static bool accept_mount(qa_mount_accept_fn accept, void *ctx,
                         const qa_content_mount *mount) {
  return accept == NULL || accept(ctx, mount);
}
static const qa_prefix_mount_order *find_prefix(
    const qa_resolved_mount_plan *plan, const char *requested_path) {
  for (size_t i = 0; i < plan->prefix_order_count; i++)
    if (starts_with_ci(requested_path, plan->prefix_orders[i].prefix))
      return &plan->prefix_orders[i];
  return NULL;
}

static bool mounted_open_impl(qa_content_mounted *mounted,
                              const qa_resolved_mount_plan *plan,
                              const char *path, qa_mount_accept_fn accept,
                              void *accept_ctx, opened_record **records,
                              size_t *count, size_t *capacity,
                              qa_content_opened_resource *out) {
  qa_content_mounted_assert_open(mounted);
  char *requested_path = qa_normalize_resource_path(path);
  if (requested_path == NULL) oom_error();
  if (mounted->user_mount_count != 0) {
    for (size_t rank = 0; rank < plan->default_order_count; rank++) {
      qa_mount_id id = plan->default_order[rank];
      if (!is_user_mount(mounted, id)) continue;
      mounted_source *source = source_for(mounted, id);
      if (source == NULL || !accept_mount(accept, accept_ctx, &source->mount))
        continue;
      source_read read;
      bool found = read_source(mounted, source, requested_path, &read);
      qa_content_mounted_assert_open(mounted);
      if (found) {
        qa_resource_resolution resolution;
        memset(&resolution, 0, sizeof resolution);
        resolution.kind = QA_RESOURCE_RESOLUTION_DEFAULT_ORDER;
        resolution.plan = plan->id;
        resolution.rank = (int64_t)rank;
        opened_record *record = store_opened(mounted, records, count, capacity,
                                             requested_path, &read, resolution);
        out->reference = &record->reference;
        out->bytes = read.bytes;
        out->byte_length = read.byte_length;
        free(requested_path);
        return true;
      }
    }
  }
  for (size_t li = 0; li < mounted->options.link_count; li++) {
    const qa_resource_link *link = &mounted->options.links[li];
    if (!starts_with(requested_path, link->source_prefix)) continue;
    mounted_source *source = source_for(mounted, link->mount);
    if (source == NULL) plain_error("Unknown link mount: %s", link->mount);
    if (!accept_mount(accept, accept_ctx, &source->mount)) {
      free(requested_path);
      return false;
    }
    const char *target_prefix =
        link->target_prefix != NULL ? link->target_prefix : "";
    size_t joined_length = strlen(target_prefix) +
                           strlen(requested_path + strlen(link->source_prefix)) + 1;
    char *joined = (char *)malloc(joined_length);
    if (joined == NULL) oom_error();
    (void)snprintf(joined, joined_length, "%s%s", target_prefix,
                   requested_path + strlen(link->source_prefix));
    char *target_path = qa_normalize_resource_path(joined);
    free(joined);
    if (target_path == NULL) oom_error();
    source_read read;
    bool found = read_source(mounted, source, target_path, &read);
    qa_content_mounted_assert_open(mounted);
    if (!found) {
      free(target_path);
      free(requested_path);
      return false;
    }
    qa_resource_resolution resolution;
    memset(&resolution, 0, sizeof resolution);
    resolution.kind = QA_RESOURCE_RESOLUTION_LINK;
    resolution.plan = plan->id;
    resolution.source_prefix = link->source_prefix;
    resolution.target_path = target_path;
    opened_record *record = store_opened(mounted, records, count, capacity,
                                         requested_path, &read, resolution);
    free(target_path);
    out->reference = &record->reference;
    out->bytes = read.bytes;
    out->byte_length = read.byte_length;
    free(requested_path);
    return true;
  }
  const qa_prefix_mount_order *prefix = find_prefix(plan, requested_path);
  const qa_mount_id *order = prefix != NULL ? prefix->mounts : plan->default_order;
  size_t order_count =
      prefix != NULL ? prefix->mount_count : plan->default_order_count;
  for (size_t rank = 0; rank < order_count; rank++) {
    qa_mount_id id = order[rank];
    mounted_source *source = source_for(mounted, id);
    if (source == NULL) plain_error("Unknown mounted source: %s", id);
    if (is_user_mount(mounted, id)) continue;
    if (!accept_mount(accept, accept_ctx, &source->mount)) continue;
    source_read read;
    bool found = read_source(mounted, source, requested_path, &read);
    qa_content_mounted_assert_open(mounted);
    if (found) {
      qa_resource_resolution resolution;
      memset(&resolution, 0, sizeof resolution);
      resolution.kind = prefix == NULL
                            ? QA_RESOURCE_RESOLUTION_DEFAULT_ORDER
                            : QA_RESOURCE_RESOLUTION_PREFIX_ORDER;
      resolution.plan = plan->id;
      resolution.rank = (int64_t)rank;
      resolution.prefix = prefix != NULL ? prefix->prefix : NULL;
      opened_record *record = store_opened(mounted, records, count, capacity,
                                           requested_path, &read, resolution);
      out->reference = &record->reference;
      out->bytes = read.bytes;
      out->byte_length = read.byte_length;
      free(requested_path);
      return true;
    }
  }
  free(requested_path);
  return false;
}

bool qa_content_mounted_open(qa_content_mounted *mounted, const char *path,
                             qa_mount_accept_fn accept, void *accept_ctx,
                             qa_content_opened_resource *out) {
  return mounted_open_impl(mounted, &mounted->plan, path, accept, accept_ctx,
                           &mounted->opened, &mounted->opened_count,
                           &mounted->opened_capacity, out);
}
bool qa_content_mounted_resolve(qa_content_mounted *mounted, const char *path,
                                const qa_resolved_resource_reference **out) {
  qa_content_opened_resource opened;
  memset(&opened, 0, sizeof opened);
  if (!qa_content_mounted_open(mounted, path, NULL, NULL, &opened)) {
    *out = NULL;
    return false;
  }
  free(opened.bytes);
  *out = opened.reference;
  return true;
}

static uint8_t *mounted_read_impl(qa_content_mounted *mounted,
                                  const qa_resolved_resource_reference *resource,
                                  const char *path, size_t *out_length,
                                  opened_record **records, size_t *count,
                                  size_t *capacity,
                                  const qa_resolved_mount_plan *open_plan);

uint8_t *qa_content_mounted_read_path(qa_content_mounted *mounted,
                                      const char *path, size_t *out_length) {
  return mounted_read_impl(mounted, NULL, path, out_length, &mounted->opened,
                           &mounted->opened_count, &mounted->opened_capacity,
                           &mounted->plan);
}
uint8_t *qa_content_mounted_read_reference(
    qa_content_mounted *mounted, const qa_resolved_resource_reference *resource,
    size_t *out_length) {
  return mounted_read_impl(mounted, resource, NULL, out_length,
                           &mounted->opened, &mounted->opened_count,
                           &mounted->opened_capacity, &mounted->plan);
}

static void store_reference_direct(qa_content_mounted *mounted,
                                   opened_record **records_ptr, size_t *count,
                                   size_t *capacity,
                                   const qa_resolved_resource_reference *resource) {
  qa_mount_id mount_id =
      resource->provenance.kind == QA_CONTENT_MOUNT_ARCHIVE
          ? resource->provenance.archive_mount->identity.id
          : resource->provenance.loose_mount->identity.id;
  int64_t key = opened_key(mounted, mount_id, resource->requested_path);
  opened_record *records = *records_ptr;
  opened_record *record = NULL;
  for (size_t i = 0; i < *count; i++)
    if (records[i].key == key) record = &records[i];
  if (record == NULL) {
    if (*count == *capacity) {
      size_t next = *capacity == 0 ? 8 : *capacity * 2;
      opened_record *grown =
          (opened_record *)realloc(records, next * sizeof *grown);
      if (grown == NULL) oom_error();
      records = grown;
      *records_ptr = grown;
      *capacity = next;
    }
    record = &records[*count];
    memset(record, 0, sizeof *record);
    record->key = key;
    (*count)++;
  } else {
    int64_t keep_key = record->key;
    free_opened_record(record);
    record->key = keep_key;
  }
  record->requested_path = copy_text_or_oom(resource->requested_path);
  record->member_path = copy_text_or_oom(resource->provenance.member_path);
  record->digest = copy_text_or_oom(resource->digest);
  record->id = copy_text_or_oom(resource->id);
  record->prefix = copy_text(resource->resolution.prefix);
  record->source_prefix = copy_text(resource->resolution.source_prefix);
  record->target_path = copy_text(resource->resolution.target_path);
  record->reference = *resource;
  record->reference.id = record->id;
  record->reference.requested_path = record->requested_path;
  record->reference.digest = record->digest;
  record->reference.provenance.member_path = record->member_path;
  record->reference.resolution.prefix = record->prefix;
  record->reference.resolution.source_prefix = record->source_prefix;
  record->reference.resolution.target_path = record->target_path;
}

static uint8_t *mounted_read_impl(qa_content_mounted *mounted,
                                  const qa_resolved_resource_reference *resource,
                                  const char *path, size_t *out_length,
                                  opened_record **records, size_t *count,
                                  size_t *capacity,
                                  const qa_resolved_mount_plan *open_plan) {
  qa_content_mounted_assert_open(mounted);
  if (path != NULL) {
    qa_content_opened_resource opened;
    memset(&opened, 0, sizeof opened);
    if (!mounted_open_impl(mounted, open_plan, path, NULL, NULL, records, count,
                           capacity, &opened))
      plain_error("Resource not found: %s", path);
    if (out_length != NULL) *out_length = opened.byte_length;
    return opened.bytes;
  }
  qa_mount_id mount_id =
      resource->provenance.kind == QA_CONTENT_MOUNT_ARCHIVE
          ? resource->provenance.archive_mount->identity.id
          : resource->provenance.loose_mount->identity.id;
  mounted_source *source = source_for(mounted, mount_id);
  const qa_mount_identity *want =
      resource->provenance.kind == QA_CONTENT_MOUNT_ARCHIVE
          ? &resource->provenance.archive_mount->identity
          : &resource->provenance.loose_mount->identity;
  if (source == NULL ||
      !text_equal(mount_identity(&source->mount)->content, want->content) ||
      mount_identity(&source->mount)->generation != want->generation)
    plain_error("Stale resource mount: %s", resource->id);
  uint8_t *bytes = NULL;
  size_t length = 0;
  if (resource->provenance.kind == QA_CONTENT_MOUNT_ARCHIVE) {
    if (source->mount.kind != QA_CONTENT_MOUNT_ARCHIVE ||
        !text_equal(source->mount.as.archive.archive_digest,
                    resource->provenance.archive_mount->archive_digest))
      plain_error("Archive identity changed: %s", resource->id);
    const qa_archive_entry *entry = NULL;
    for (size_t i = 0; i < source->archive->entry_count; i++)
      if (source->archive->entries[i].ordinal ==
          resource->provenance.member_index)
        entry = &source->archive->entries[i];
    if (entry == NULL ||
        !text_equal(entry->path, resource->provenance.member_path))
      plain_error("Archive member identity changed: %s", resource->id);
    if (!source_allowed(mounted, source, entry->path))
      plain_error("Resource excluded by pure policy: %s",
                  resource->requested_path);
    bytes = qa_archive_read_entry_entry(source->archive, entry, &length);
    if (bytes == NULL && length != 0) oom_error();
    bool seen = false;
    for (size_t i = 0; i < mounted->referenced_count; i++)
      if (text_equal(mounted->referenced[i].identity.id,
                     source->mount.as.archive.identity.id))
        seen = true;
    if (!seen) {
      if (mounted->referenced_count == mounted->referenced_capacity) {
        size_t next = mounted->referenced_capacity == 0
                          ? 4
                          : mounted->referenced_capacity * 2;
        qa_archive_mount *grown = (qa_archive_mount *)realloc(
            mounted->referenced, next * sizeof *grown);
        if (grown == NULL) oom_error();
        mounted->referenced = grown;
        mounted->referenced_capacity = next;
      }
      mounted->referenced[mounted->referenced_count++] =
          source->mount.as.archive;
    }
  } else {
    if (source->mount.kind != QA_CONTENT_MOUNT_LOOSE ||
        !text_equal(source->mount.as.loose.root_path,
                    resource->provenance.loose_mount->root_path))
      plain_error("Loose root identity changed: %s", resource->id);
    source_read read;
    if (!read_source(mounted, source, resource->provenance.member_path, &read))
      plain_error("Resource is no longer available: %s",
                  resource->requested_path);
    free(read.member_path);
    bytes = read.bytes;
    length = read.byte_length;
  }
  qa_content_mounted_assert_open(mounted);
  char *digest = qa_digest_bytes(bytes, length);
  bool changed = (int64_t)length != resource->byte_length ||
                 !text_equal(digest, resource->digest);
  free(digest);
  if (changed) {
    free(bytes);
    plain_error("Resource bytes changed since resolution: %s",
                resource->requested_path);
  }
  store_reference_direct(mounted, records, count, capacity, resource);
  if (out_length != NULL) *out_length = length;
  return bytes;
}

void qa_content_mounted_invalidate_listings(qa_content_mounted *mounted) {
  clear_listings(mounted);
}

static const listing_cache *listing_for(qa_content_mounted *mounted,
                                        const char *directory) {
  for (size_t i = 0; i < mounted->listing_count; i++)
    if (strcmp(mounted->listings[i].directory, directory) == 0)
      return &mounted->listings[i];
  if (mounted->listing_count == mounted->listing_capacity) {
    size_t next = mounted->listing_capacity == 0 ? 8 : mounted->listing_capacity * 2;
    listing_cache *grown =
        (listing_cache *)realloc(mounted->listings, next * sizeof *grown);
    if (grown == NULL) oom_error();
    mounted->listings = grown;
    mounted->listing_capacity = next;
  }
  listing_cache *cache = &mounted->listings[mounted->listing_count];
  memset(cache, 0, sizeof *cache);
  cache->directory = copy_text_or_oom(directory);
  mounted->listing_count++;
  DIR *dir = opendir(directory);
  if (dir == NULL) {
    int error_number = errno;
    if (qa_is_missing_file(error_number)) {
      cache->missing = true;
      return cache;
    }
    fprintf(stderr, "Error: %s: %s\n", directory, strerror(error_number));
    abort();
  }
  struct dirent *entry;
  size_t capacity = 0;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;
    if (cache->count == capacity) {
      size_t next = capacity == 0 ? 16 : capacity * 2;
      listing_entry *grown =
          (listing_entry *)realloc(cache->entries, next * sizeof *grown);
      if (grown == NULL) oom_error();
      cache->entries = grown;
      capacity = next;
    }
    cache->entries[cache->count].name = copy_text_or_oom(entry->d_name);
    cache->entries[cache->count].type = entry->d_type;
    cache->count++;
  }
  (void)closedir(dir);
  return cache;
}

static bool entry_kind(const listing_cache *cache, size_t index,
                       const char *current_path, bool want_file,
                       bool want_directory, bool *is_link, bool *unknown) {
  unsigned char type = cache->entries[index].type;
  *is_link = type == DT_LNK;
  *unknown = type != DT_REG && type != DT_DIR && type != DT_LNK;
  if (want_file && type == DT_REG) return true;
  if (want_directory && type == DT_DIR) return true;
  if (*unknown) {
    char *joined = resolve2(current_path, cache->entries[index].name);
    struct stat info;
    bool result = false;
    if (joined != NULL && lstat(joined, &info) == 0) {
      *is_link = S_ISLNK(info.st_mode);
      *unknown = !S_ISREG(info.st_mode) && !S_ISDIR(info.st_mode) && !*is_link;
      result = (want_file && S_ISREG(info.st_mode)) ||
               (want_directory && S_ISDIR(info.st_mode));
    }
    free(joined);
    return result;
  }
  (void)current_path;
  return false;
}

static bool probe_loose(qa_content_mounted *mounted, const char *root_path,
                        const char *member_path) {
  char *normalized = qa_normalize_resource_path(member_path);
  if (normalized == NULL) oom_error();
  bool exact = loose_comparison_of(mounted) == QA_CONTENT_PATH_EXACT;
  char *current = resolve2(root_path, NULL);
  if (current == NULL) oom_error();
  size_t length = strlen(normalized), start = 0;
  size_t part_index = 0;
  size_t part_total = 1;
  for (size_t i = 0; i < length; i++)
    if (normalized[i] == '/') part_total++;
  bool result = false;
  for (size_t i = 0; i <= length; i++) {
    if (i != length && normalized[i] != '/') continue;
    size_t part_length = i - start;
    char part[PATH_MAX];
    if (part_length >= sizeof part) {
      result = false;
      break;
    }
    memcpy(part, normalized + start, part_length);
    part[part_length] = '\0';
    const listing_cache *cache = listing_for(mounted, current);
    if (cache->missing) {
      result = false;
      break;
    }
    long exact_index = -1;
    long ci_index = -1;
    size_t ci_count = 0;
    for (size_t e = 0; e < cache->count; e++) {
      if (strcmp(cache->entries[e].name, part) == 0) exact_index = (long)e;
      char *lower_name = lowered_copy(cache->entries[e].name);
      char *lower_part = lowered_copy(part);
      bool equal = lower_name != NULL && lower_part != NULL &&
                   strcmp(lower_name, lower_part) == 0;
      free(lower_name);
      free(lower_part);
      if (equal) {
        if (ci_count == 0) ci_index = (long)e;
        ci_count++;
      }
    }
    long chosen;
    if (exact) {
      chosen = exact_index;
    } else if (exact_index >= 0) {
      chosen = exact_index;
    } else {
      if (ci_count > 1) {
        free(current);
        free(normalized);
        plain_error("Ambiguous content path: %s", member_path);
      }
      chosen = ci_index;
    }
    if (chosen < 0) {
      result = false;
      break;
    }
    bool is_link = false, unknown = false;
    bool is_file = entry_kind(cache, (size_t)chosen, current, true, false,
                              &is_link, &unknown);
    bool is_directory = entry_kind(cache, (size_t)chosen, current, false, true,
                                   &is_link, &unknown);
    char *next = resolve2(current, cache->entries[chosen].name);
    if (next == NULL) oom_error();
    free(current);
    current = next;
    if (is_link || (!is_file && !is_directory)) {
      char *found = NULL;
      result = qa_find_content_path(root_path, member_path,
                                    loose_comparison_of(mounted), &found) &&
               found != NULL;
      if (result) {
        struct stat info;
        result = stat(found, &info) == 0 && S_ISREG(info.st_mode);
      }
      free(found);
      break;
    }
    if (part_index == part_total - 1) {
      result = is_file;
      break;
    }
    if (!is_directory) {
      result = false;
      break;
    }
    part_index++;
    start = i + 1;
  }
  free(current);
  free(normalized);
  return result;
}

static bool probe_source(qa_content_mounted *mounted, mounted_source *source,
                         const char *member_path) {
  if (!source_allowed(mounted, source, member_path)) return false;
  if (source->mount.kind == QA_CONTENT_MOUNT_ARCHIVE) {
    size_t total = qa_archive_find_entries(
        source->archive, member_path, QA_ARCHIVE_PATH_CASE_INSENSITIVE, NULL, 0);
    const qa_archive_entry **found = (const qa_archive_entry **)malloc(
        (total ? total : 1) * sizeof *found);
    if (found == NULL) oom_error();
    (void)qa_archive_find_entries(source->archive, member_path,
                                  QA_ARCHIVE_PATH_CASE_INSENSITIVE, found,
                                  total);
    bool any = false;
    for (size_t i = 0; i < total; i++)
      if (!found[i]->is_directory) any = true;
    free(found);
    return any;
  }
  return probe_loose(mounted, source->mount.as.loose.root_path, member_path);
}

bool qa_content_mounted_exists(qa_content_mounted *mounted, const char *path) {
  qa_content_mounted_assert_open(mounted);
  const qa_resolved_mount_plan *plan = &mounted->plan;
  char *requested_path = qa_normalize_resource_path(path);
  if (requested_path == NULL) oom_error();
  if (mounted->user_mount_count != 0) {
    for (size_t i = 0; i < plan->default_order_count; i++) {
      qa_mount_id id = plan->default_order[i];
      if (!is_user_mount(mounted, id)) continue;
      mounted_source *source = source_for(mounted, id);
      if (source != NULL && probe_source(mounted, source, requested_path)) {
        free(requested_path);
        return true;
      }
    }
  }
  for (size_t li = 0; li < mounted->options.link_count; li++) {
    const qa_resource_link *link = &mounted->options.links[li];
    if (!starts_with(requested_path, link->source_prefix)) continue;
    mounted_source *source = source_for(mounted, link->mount);
    if (source == NULL) plain_error("Unknown link mount: %s", link->mount);
    const char *target_prefix =
        link->target_prefix != NULL ? link->target_prefix : "";
    size_t joined_length = strlen(target_prefix) +
                           strlen(requested_path + strlen(link->source_prefix)) + 1;
    char *joined = (char *)malloc(joined_length);
    if (joined == NULL) oom_error();
    (void)snprintf(joined, joined_length, "%s%s", target_prefix,
                   requested_path + strlen(link->source_prefix));
    char *target_path = qa_normalize_resource_path(joined);
    free(joined);
    if (target_path == NULL) oom_error();
    bool result = probe_source(mounted, source, target_path);
    free(target_path);
    free(requested_path);
    return result;
  }
  const qa_prefix_mount_order *prefix = find_prefix(plan, requested_path);
  const qa_mount_id *order = prefix != NULL ? prefix->mounts : plan->default_order;
  size_t order_count =
      prefix != NULL ? prefix->mount_count : plan->default_order_count;
  for (size_t i = 0; i < order_count; i++) {
    qa_mount_id id = order[i];
    mounted_source *source = source_for(mounted, id);
    if (source == NULL) plain_error("Unknown mounted source: %s", id);
    if (is_user_mount(mounted, id)) continue;
    if (probe_source(mounted, source, requested_path)) {
      free(requested_path);
      return true;
    }
  }
  free(requested_path);
  return false;
}

static size_t path_depth(const char *value) {
  size_t depth = 0;
  for (const char *p = value; *p != '\0'; p++)
    if (*p == '/' || *p == '\\') depth++;
  return depth;
}
static bool has_suffix_ci(const char *name, const char *extension) {
  size_t name_length = strlen(name), extension_length = strlen(extension);
  if (name_length < extension_length) return false;
  for (size_t i = 0; i < extension_length; i++) {
    char a = name[name_length - extension_length + i];
    char b = extension[i];
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    if (a != b) return false;
  }
  return true;
}

char **qa_content_mounted_list_files(qa_content_mounted *mounted,
                                     const char *path, const char *extension,
                                     size_t *out_count) {
  qa_content_mounted_assert_open(mounted);
  if (strlen(path) >= 256)
    qa_range_error("Q3 file listing exceeds source path representation");
  char *directory = copy_text_or_oom(path);
  size_t directory_length = strlen(directory);
  if (directory_length > 0 &&
      (directory[directory_length - 1] == '/' ||
       directory[directory_length - 1] == '\\'))
    directory[--directory_length] = '\0';
  if (directory_length != 0) {
    char *normalized = qa_normalize_resource_path(directory);
    if (normalized == NULL) oom_error();
    free(normalized);
  }
  char *with_slash = (char *)malloc(directory_length + 2);
  if (with_slash == NULL) oom_error();
  (void)snprintf(with_slash, directory_length + 2, "%s/", directory);
  const qa_prefix_mount_order *prefix = find_prefix(&mounted->plan, with_slash);
  free(with_slash);
  const qa_mount_id *order =
      prefix != NULL ? prefix->mounts : mounted->plan.default_order;
  size_t order_count =
      prefix != NULL ? prefix->mount_count : mounted->plan.default_order_count;
  char **names = NULL;
  size_t count = 0, capacity = 0;
  char **seen = NULL;
  size_t seen_count = 0, seen_capacity = 0;
  for (size_t oi = 0; oi < order_count; oi++) {
    qa_mount_id id = order[oi];
    mounted_source *source = source_for(mounted, id);
    if (source == NULL) plain_error("Unknown mounted source: %s", id);
    if (source->mount.kind == QA_CONTENT_MOUNT_ARCHIVE) {
      for (size_t e = 0; e < source->archive->entry_count; e++) {
        const char *name = source->archive->entries[e].path;
        const char *slash = strrchr(name, '/');
        const char *backslash = strrchr(name, '\\');
        long last_separator = -1;
        if (slash != NULL) last_separator = (long)(slash - name);
        if (backslash != NULL && (long)(backslash - name) > last_separator)
          last_separator = (long)(backslash - name);
        long floor_separator = last_separator > 0 ? last_separator : 0;
        if (!source_allowed(mounted, source, name)) continue;
        if ((long)path_depth(name) - (long)path_depth(path) > 2) continue;
        if ((long)directory_length > floor_separator) continue;
        bool head_equal = true;
        for (size_t i = 0; i < directory_length; i++) {
          char a = name[i], b = directory[i];
          if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
          if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
          if (a != b) head_equal = false;
        }
        if (!head_equal) continue;
        if (!has_suffix_ci(name, extension)) continue;
        const char *relative_name =
            name + (directory_length == 0 ? 0 : directory_length + 1);
        if (count >= 4095) continue;
        char *lower = lowered_copy(relative_name);
        bool duplicate = false;
        for (size_t s = 0; s < seen_count; s++)
          if (lower != NULL && strcmp(seen[s], lower) == 0) duplicate = true;
        if (duplicate) { free(lower); continue; }
        if (seen_count == seen_capacity) {
          size_t next = seen_capacity == 0 ? 16 : seen_capacity * 2;
          char **grown = (char **)realloc(seen, next * sizeof *grown);
          if (grown == NULL) oom_error();
          seen = grown;
          seen_capacity = next;
        }
        seen[seen_count++] = lower;
        if (count == capacity) {
          size_t next = capacity == 0 ? 16 : capacity * 2;
          char **grown = (char **)realloc(names, next * sizeof *grown);
          if (grown == NULL) oom_error();
          names = grown;
          capacity = next;
        }
        names[count++] = copy_text_or_oom(relative_name);
      }
    } else {
      if (!is_user_mount(mounted, mount_identity(&source->mount)->id) &&
          (mounted->options.q3_restriction_demo ||
           (mounted->options.has_pure &&
            mounted->options.pure.archive_count != 0)))
        continue;
      char *location = NULL;
      if (directory_length == 0) {
        location = copy_text_or_oom(source->mount.as.loose.root_path);
      } else if (!qa_find_content_path(source->mount.as.loose.root_path,
                                       directory,
                                       loose_comparison_of(mounted),
                                       &location)) {
        continue;
      }
      DIR *dir = opendir(location);
      if (dir == NULL) {
        int error_number = errno;
        free(location);
        if (qa_is_missing_file(error_number)) continue;
        fprintf(stderr, "Error: %s: %s\n", directory, strerror(error_number));
        abort();
      }
      free(location);
      qa_content_mounted_assert_open(mounted);
      struct dirent *entry;
      bool want_directories = strcmp(extension, "/") == 0;
      while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
          continue;
        bool is_link = entry->d_type == DT_LNK;
        bool is_directory = entry->d_type == DT_DIR;
        if (entry->d_type == DT_UNKNOWN) {
          struct stat info;
          char joined[PATH_MAX];
          (void)snprintf(joined, sizeof joined, "%s/%s",
                         source->mount.as.loose.root_path, entry->d_name);
          if (lstat(joined, &info) == 0) {
            is_link = S_ISLNK(info.st_mode);
            is_directory = S_ISDIR(info.st_mode);
          }
        }
        if (is_link) continue;
        if (want_directories != is_directory) continue;
        if (!want_directories && !has_suffix_ci(entry->d_name, extension))
          continue;
        if (count >= 4095) continue;
        char *lower = lowered_copy(entry->d_name);
        bool duplicate = false;
        for (size_t s = 0; s < seen_count; s++)
          if (lower != NULL && strcmp(seen[s], lower) == 0) duplicate = true;
        if (duplicate) { free(lower); continue; }
        if (seen_count == seen_capacity) {
          size_t next = seen_capacity == 0 ? 16 : seen_capacity * 2;
          char **grown = (char **)realloc(seen, next * sizeof *grown);
          if (grown == NULL) oom_error();
          seen = grown;
          seen_capacity = next;
        }
        seen[seen_count++] = lower;
        if (count == capacity) {
          size_t next = capacity == 0 ? 16 : capacity * 2;
          char **grown = (char **)realloc(names, next * sizeof *grown);
          if (grown == NULL) oom_error();
          names = grown;
          capacity = next;
        }
        names[count++] = copy_text_or_oom(entry->d_name);
      }
      (void)closedir(dir);
    }
  }
  qa_content_mounted_assert_open(mounted);
  qa_content_free_string_list(seen, seen_count);
  free(directory);
  if (out_count != NULL) *out_count = count;
  return names;
}

void qa_content_mounted_close(qa_content_mounted *mounted) {
  if (mounted == NULL || mounted->closed) return;
  mounted->closed = true;
  for (size_t i = 0; i < mounted->opened_count; i++)
    free_opened_record(&mounted->opened[i]);
  mounted->opened_count = 0;
  clear_listings(mounted);
  if (mounted->owner_kind == 0 /* OWNER_NONE: owned */) {
    for (size_t i = 0; i < mounted->source_count; i++)
      if (mounted->sources[i].archive != NULL)
        qa_archive_close(mounted->sources[i].archive);
  }
}

void qa_content_mounted_destroy(qa_content_mounted *mounted) {
  if (mounted == NULL) return;
  bool owned = mounted->owner_kind == 0;
  qa_content_mounted_close(mounted);
  if (owned) {
    for (size_t i = 0; i < mounted->source_count; i++)
      qa_archive_destroy(mounted->sources[i].archive);
  }
  free(mounted->sources);
  for (size_t i = 0; i < mounted->opened_count; i++)
    free_opened_record(&mounted->opened[i]);
  free(mounted->opened);
  free(mounted->opened_view);
  for (size_t i = 0; i < mounted->opened_key_count; i++)
    free(mounted->opened_keys[i].path);
  free(mounted->opened_keys);
  free(mounted->referenced);
  free(mounted->user_mounts);
  free(mounted->plan_mounts);
  free(mounted->plan_default_order);
  for (size_t i = 0; i < mounted->plan.prefix_order_count; i++)
    free(mounted->plan_prefix_mounts[i]);
  free(mounted->plan_prefix_mounts);
  free(mounted->plan_prefixes);
  free(mounted->pure_archives);
  free(mounted->links);
  free(mounted);
}

/* ---- borrowing --------------------------------------------------- */

qa_content_mounted *qa_content_mounted_borrow_mount_plan(
    qa_content_mounted *mounted, const qa_resolved_mount_plan *plan,
    const qa_open_mount_options *options_in) {
  qa_content_mounted_assert_open(mounted);
  qa_open_mount_options options =
      options_in != NULL ? *options_in : empty_options();
  bool parent_demo = mounted->options.q3_restriction_demo;
  /* TS checks `q3Restriction !== undefined` on the parent; our bool
   * cannot distinguish unset from false, and "demo" is the only
   * value, so a demo parent forces demo (the observable case). */
  if (parent_demo) options.q3_restriction_demo = true;
  if (options.q3_restriction_demo != parent_demo) return NULL;
  qa_content_mounted *borrowed = mounted_create_plan(plan, &options);
  if (borrowed == NULL) return NULL;
  mounted_source *sources = (mounted_source *)calloc(
      plan->mount_count ? plan->mount_count : 1, sizeof *sources);
  if (sources == NULL) oom_error();
  for (size_t i = 0; i < plan->mount_count; i++) {
    mounted_source *source =
        source_for(mounted, mount_identity(&plan->mounts[i])->id);
    if (source == NULL || !same_mount(&source->mount, &plan->mounts[i])) {
      free(sources);
      qa_content_mounted_destroy(borrowed);
      return NULL;
    }
    sources[i] = *source;
  }
  qa_content_path_comparison want_loose =
      options.has_loose_comparison ? options.loose_comparison
                                   : QA_CONTENT_PATH_EXACT;
  qa_content_path_comparison parent_loose =
      mounted->options.has_loose_comparison
          ? mounted->options.loose_comparison
          : QA_CONTENT_PATH_EXACT;
  if (want_loose != parent_loose) {
    free(sources);
    qa_content_mounted_destroy(borrowed);
    return NULL;
  }
  if (options.link_count != mounted->options.link_count) {
    free(sources);
    qa_content_mounted_destroy(borrowed);
    return NULL;
  }
  for (size_t i = 0; i < options.link_count; i++) {
    const qa_resource_link *link = &options.links[i];
    const qa_resource_link *parent = &mounted->options.links[i];
    if (!text_equal(link->mount, parent->mount) ||
        !text_equal(link->source_prefix, parent->source_prefix) ||
        !text_equal(link->target_prefix, parent->target_prefix)) {
      free(sources);
      qa_content_mounted_destroy(borrowed);
      return NULL;
    }
  }
  size_t parent_pure_count =
      mounted->options.has_pure ? mounted->options.pure.archive_count : 0;
  size_t pure_count = options.has_pure ? options.pure.archive_count : 0;
  size_t expected_count = 0;
  bool pure_match = true;
  for (size_t i = 0; i < parent_pure_count; i++) {
    bool available = false;
    for (size_t m = 0; m < plan->mount_count; m++)
      if (plan->mounts[m].kind == QA_CONTENT_MOUNT_ARCHIVE &&
          text_equal(plan->mounts[m].as.archive.archive_digest,
                     mounted->options.pure.archives[i]))
        available = true;
    if (!available) continue;
    if (expected_count >= pure_count ||
        !text_equal(options.pure.archives[expected_count],
                    mounted->options.pure.archives[i]))
      pure_match = false;
    expected_count++;
  }
  if ((parent_pure_count > 0 && expected_count == 0) ||
      pure_count != expected_count || !pure_match) {
    free(sources);
    qa_content_mounted_destroy(borrowed);
    return NULL;
  }
  borrowed->sources = sources;
  borrowed->source_count = plan->mount_count;
  borrowed->owner_kind = 1; /* OWNER_MOUNTED */
  borrowed->owner = mounted;
  for (size_t i = 0; i < mounted->user_mount_count; i++)
    if (source_for(borrowed, mounted->user_mounts[i]) != NULL)
      add_user_mount(borrowed, mounted->user_mounts[i]);
  return borrowed;
}

qa_content_mounted *qa_content_mounted_borrow_with_loose_mount(
    qa_content_mounted *mounted, qa_loose_mount mount, qa_mount_plan_id id) {
  qa_content_mounted_assert_open(mounted);
  if (source_for(mounted, mount.identity.id) != NULL)
    plain_error("Duplicate user mount: %s", mount.identity.id);
  size_t mount_count = mounted->plan.mount_count + 1;
  qa_content_mount *mounts =
      (qa_content_mount *)malloc(mount_count * sizeof *mounts);
  qa_mount_id *default_order =
      (qa_mount_id *)malloc(mount_count * sizeof *default_order);
  if (mounts == NULL || default_order == NULL) oom_error();
  mounts[0].kind = QA_CONTENT_MOUNT_LOOSE;
  mounts[0].as.loose = mount;
  if (mounted->plan.mount_count > 0)
    memcpy(&mounts[1], mounted->plan.mounts,
           mounted->plan.mount_count * sizeof *mounts);
  default_order[0] = mount.identity.id;
  if (mounted->plan.default_order_count > 0)
    memcpy(&default_order[1], mounted->plan.default_order,
           mounted->plan.default_order_count * sizeof *default_order);
  qa_prefix_mount_order *prefixes = (qa_prefix_mount_order *)calloc(
      mounted->plan.prefix_order_count ? mounted->plan.prefix_order_count : 1,
      sizeof *prefixes);
  if (prefixes == NULL) oom_error();
  qa_mount_id **prefix_mounts = (qa_mount_id **)calloc(
      mounted->plan.prefix_order_count ? mounted->plan.prefix_order_count : 1,
      sizeof *prefix_mounts);
  if (prefix_mounts == NULL) oom_error();
  for (size_t i = 0; i < mounted->plan.prefix_order_count; i++) {
    size_t count = mounted->plan.prefix_orders[i].mount_count + 1;
    prefix_mounts[i] = (qa_mount_id *)malloc(count * sizeof *prefix_mounts[i]);
    if (prefix_mounts[i] == NULL) oom_error();
    prefix_mounts[i][0] = mount.identity.id;
    if (count > 1)
      memcpy(&prefix_mounts[i][1], mounted->plan.prefix_orders[i].mounts,
             (count - 1) * sizeof *prefix_mounts[i]);
    prefixes[i].prefix = mounted->plan.prefix_orders[i].prefix;
    prefixes[i].mounts = prefix_mounts[i];
    prefixes[i].mount_count = count;
  }
  qa_resolved_mount_plan plan;
  memset(&plan, 0, sizeof plan);
  plan.id = id;
  plan.mounts = mounts;
  plan.mount_count = mount_count;
  plan.default_order = default_order;
  plan.default_order_count = mounted->plan.default_order_count + 1;
  plan.prefix_orders = prefixes;
  plan.prefix_order_count = mounted->plan.prefix_order_count;
  qa_content_mounted *borrowed =
      mounted_create_plan(&plan, &mounted->options);
  if (borrowed == NULL) oom_error();
  mounted_source *sources =
      (mounted_source *)calloc(mount_count, sizeof *sources);
  if (sources == NULL) oom_error();
  sources[0].mount = mounts[0];
  sources[0].archive = NULL;
  if (mounted->source_count > 0)
    memcpy(&sources[1], mounted->sources,
           mounted->source_count * sizeof *sources);
  borrowed->sources = sources;
  borrowed->source_count = mount_count;
  borrowed->owner_kind = 1; /* OWNER_MOUNTED */
  borrowed->owner = mounted;
  for (size_t i = 0; i < mounted->user_mount_count; i++)
    add_user_mount(borrowed, mounted->user_mounts[i]);
  add_user_mount(borrowed, mount.identity.id);
  free(mounts);
  free(default_order);
  for (size_t i = 0; i < plan.prefix_order_count; i++) free(prefix_mounts[i]);
  free(prefix_mounts);
  free(prefixes);
  return borrowed;
}

/* ---- ordered reader ---------------------------------------------- */

struct qa_mounted_content_reader {
  qa_content_mounted *owner;
  qa_resolved_mount_plan plan;
  qa_mount_id *default_order;       /* owned */
  qa_prefix_mount_order *prefixes;  /* owned */
  qa_mount_id **prefix_mounts;      /* owned */
  opened_record *opened;
  size_t opened_count, opened_capacity;
};

qa_mounted_content_reader *qa_content_mounted_borrow_ordered_reader(
    qa_content_mounted *mounted, qa_mount_plan_id id,
    const qa_mount_id *default_order, size_t default_order_count,
    const qa_prefix_mount_order *prefix_orders, size_t prefix_order_count) {
  qa_content_mounted_assert_open(mounted);
  qa_mounted_content_reader *reader =
      (qa_mounted_content_reader *)calloc(1, sizeof *reader);
  if (reader == NULL) return NULL;
  reader->owner = mounted;
  /* resolveOrder against the owner's mounts with its pure policy. */
  qa_resolved_mount_plan input;
  memset(&input, 0, sizeof input);
  input.id = id;
  input.mounts = mounted->plan.mounts;
  input.mount_count = mounted->plan.mount_count;
  input.default_order = default_order;
  input.default_order_count = default_order_count;
  input.prefix_orders = prefix_orders;
  input.prefix_order_count = prefix_order_count;
  validate_order(default_order, default_order_count, input.mounts,
                 input.mount_count);
  for (size_t i = 0; i < prefix_order_count; i++) {
    validate_prefix_path(prefix_orders[i].prefix);
    validate_order(prefix_orders[i].mounts, prefix_orders[i].mount_count,
                   input.mounts, input.mount_count);
  }
  reader->default_order = pure_order(default_order, default_order_count,
                                     input.mounts, input.mount_count,
                                     &mounted->options);
  reader->prefixes = (qa_prefix_mount_order *)calloc(
      prefix_order_count ? prefix_order_count : 1, sizeof *reader->prefixes);
  reader->prefix_mounts = (qa_mount_id **)calloc(
      prefix_order_count ? prefix_order_count : 1, sizeof *reader->prefix_mounts);
  if (reader->prefixes == NULL || reader->prefix_mounts == NULL) oom_error();
  for (size_t i = 0; i < prefix_order_count; i++) {
    reader->prefix_mounts[i] =
        pure_order(prefix_orders[i].mounts, prefix_orders[i].mount_count,
                   input.mounts, input.mount_count, &mounted->options);
    reader->prefixes[i].prefix = prefix_orders[i].prefix;
    reader->prefixes[i].mounts = reader->prefix_mounts[i];
    reader->prefixes[i].mount_count = prefix_orders[i].mount_count;
  }
  reader->plan.id = id;
  reader->plan.mounts = mounted->plan.mounts;
  reader->plan.mount_count = mounted->plan.mount_count;
  reader->plan.default_order = reader->default_order;
  reader->plan.default_order_count = default_order_count;
  reader->plan.prefix_orders = reader->prefixes;
  reader->plan.prefix_order_count = prefix_order_count;
  return reader;
}
const qa_resolved_mount_plan *qa_mounted_content_reader_plan(
    const qa_mounted_content_reader *reader) {
  return &reader->plan;
}
bool qa_mounted_content_reader_open(qa_mounted_content_reader *reader,
                                    const char *path,
                                    qa_mount_accept_fn accept, void *accept_ctx,
                                    qa_content_opened_resource *out) {
  return mounted_open_impl(reader->owner, &reader->plan, path, accept,
                           accept_ctx, &reader->opened, &reader->opened_count,
                           &reader->opened_capacity, out);
}
bool qa_mounted_content_reader_resolve(
    qa_mounted_content_reader *reader, const char *path,
    const qa_resolved_resource_reference **out) {
  qa_content_opened_resource opened;
  memset(&opened, 0, sizeof opened);
  if (!qa_mounted_content_reader_open(reader, path, NULL, NULL, &opened)) {
    *out = NULL;
    return false;
  }
  free(opened.bytes);
  *out = opened.reference;
  return true;
}
uint8_t *qa_mounted_content_reader_read_path(qa_mounted_content_reader *reader,
                                             const char *path,
                                             size_t *out_length) {
  return mounted_read_impl(reader->owner, NULL, path, out_length,
                           &reader->opened, &reader->opened_count,
                           &reader->opened_capacity, &reader->plan);
}
uint8_t *qa_mounted_content_reader_read_reference(
    qa_mounted_content_reader *reader,
    const qa_resolved_resource_reference *resource, size_t *out_length) {
  return mounted_read_impl(reader->owner, resource, NULL, out_length,
                           &reader->opened, &reader->opened_count,
                           &reader->opened_capacity, &reader->plan);
}
void qa_mounted_content_reader_destroy(qa_mounted_content_reader *reader) {
  if (reader == NULL) return;
  for (size_t i = 0; i < reader->opened_count; i++)
    free_opened_record(&reader->opened[i]);
  free(reader->opened);
  free(reader->default_order);
  for (size_t i = 0; i < reader->plan.prefix_order_count; i++)
    free(reader->prefix_mounts[i]);
  free(reader->prefix_mounts);
  free(reader->prefixes);
  free(reader);
}

/* ---- opening plans ----------------------------------------------- */

/* q3ArchiveChecksums from src/network/q3/pure.ts (unported): nonzero
 * entries contribute CRC words in central-directory order. */
static uint32_t q3_archive_checksum(qa_archive_handle *archive) {
  if (archive->format == QA_ARCHIVE_FORMAT_PAK)
    qa_range_error("Q3 pak checksums require a ZIP/PK3 central directory");
  size_t nonzero = 0;
  for (size_t i = 0; i < archive->entry_count; i++)
    if (archive->entries[i].byte_length > 0) nonzero++;
  uint8_t *bytes = (uint8_t *)malloc(nonzero ? nonzero * 4 : 1);
  if (bytes == NULL) oom_error();
  size_t at = 0;
  for (size_t i = 0; i < archive->entry_count; i++) {
    const qa_archive_entry *entry = &archive->entries[i];
    if (entry->byte_length <= 0) continue;
    if (entry->format == QA_ARCHIVE_FORMAT_PAK) {
      free(bytes);
      qa_range_error("Q3 ZIP directory contains a PAK entry");
    }
    uint32_t crc = entry->crc32;
    bytes[at++] = (uint8_t)(crc & 0xffu);
    bytes[at++] = (uint8_t)((crc >> 8) & 0xffu);
    bytes[at++] = (uint8_t)((crc >> 16) & 0xffu);
    bytes[at++] = (uint8_t)((crc >> 24) & 0xffu);
  }
  uint32_t checksum = qa_block_checksum(bytes, at);
  free(bytes);
  return checksum;
}

static void check_demo_archive(qa_archive_handle *archive,
                               const qa_archive_mount *mount,
                               bool scope_variant) {
  if (archive->format != QA_ARCHIVE_FORMAT_PK3)
    plain_error("Restricted Q3 content requires PK3 archives: %s",
                mount->archive_path);
  uint32_t checksum = q3_archive_checksum(archive);
  if (checksum != 437558517u) {
    if (scope_variant) {
      plain_error("Corrupted demo pak0.pk3: %s", mount->archive_path);
    } else {
      char buffer[32];
      (void)snprintf(buffer, sizeof buffer, "%u", checksum);
      plain_error("Corrupted demo pak0.pk3: %s", buffer);
    }
  }
}

static void close_sources(mounted_source *sources, size_t count) {
  for (size_t i = 0; i < count; i++)
    qa_archive_destroy(sources[i].archive);
  free(sources);
}

qa_content_mounted *qa_open_mount_plan(const qa_resolved_mount_plan *plan,
                                       const qa_open_mount_options *options) {
  qa_content_mounted *mounted = mounted_create_plan(plan, options);
  if (mounted == NULL) return NULL;
  mounted_source *sources = (mounted_source *)calloc(
      plan->mount_count ? plan->mount_count : 1, sizeof *sources);
  if (sources == NULL) oom_error();
  size_t built = 0;
  /* TS aborts on failure after closing built archives; C aborts
   * leak the shell, matching the crash-handler behavior. */
  for (size_t i = 0; i < plan->mount_count; i++) {
    const qa_content_mount *mount = &plan->mounts[i];
    sources[built].mount = *mount;
    sources[built].archive = NULL;
    if (mount->kind == QA_CONTENT_MOUNT_ARCHIVE) {
      char *digest = qa_digest_file(mount->as.archive.archive_path);
      bool matches =
          digest != NULL &&
          text_equal(digest, mount->as.archive.archive_digest);
      free(digest);
      if (!matches) {
        close_sources(sources, built);
        qa_content_mounted_destroy(mounted);
        plain_error("Archive bytes changed before mount: %s",
                    mount->as.archive.archive_path);
      }
      qa_archive_handle *archive = qa_open_archive(
          mount->as.archive.archive_path, true, mount->as.archive.format);
      if (archive == NULL) oom_error();
      sources[built].archive = archive;
      built++;
      if (mounted->options.q3_restriction_demo)
        check_demo_archive(archive, &mount->as.archive, false);
    } else {
      built++;
    }
  }
  mounted->sources = sources;
  mounted->source_count = built;
  return mounted;
}

bool qa_can_download_resource(const qa_resolved_resource_reference *resource) {
  const char *content =
      resource->provenance.kind == QA_CONTENT_MOUNT_ARCHIVE
          ? resource->provenance.archive_mount->identity.content
          : resource->provenance.loose_mount->identity.content;
  return !(starts_with(content, "q2:") &&
           resource->provenance.kind == QA_CONTENT_MOUNT_ARCHIVE &&
           starts_with_ci(resource->requested_path, "maps/"));
}

/* ---- preparation scope ------------------------------------------- */

typedef struct scope_archive {
  char *path;     /* owned */
  char *digest;   /* owned */
  qa_archive_format format;
  qa_archive_handle *archive;
} scope_archive;
struct qa_mount_preparation_scope {
  scope_archive *archives;
  size_t count, capacity;
  bool closed;
  bool disposed;
};

qa_mount_preparation_scope *qa_mount_preparation_scope_create(void) {
  return (qa_mount_preparation_scope *)calloc(
      1, sizeof(qa_mount_preparation_scope));
}
void qa_mount_preparation_scope_assert_open(
    const qa_mount_preparation_scope *scope) {
  if (scope->closed) plain_error("%s", "Mount preparation scope is closed");
}
static qa_archive_handle *scope_archive_for(
    qa_mount_preparation_scope *scope, const qa_archive_mount *mount) {
  qa_mount_preparation_scope_assert_open(scope);
  for (size_t i = 0; i < scope->count; i++)
    if (text_equal(scope->archives[i].path, mount->archive_path) &&
        text_equal(scope->archives[i].digest, mount->archive_digest) &&
        scope->archives[i].format == mount->format)
      return scope->archives[i].archive;
  qa_archive_source storage;
  if (!qa_file_source_init(&storage, mount->archive_path)) oom_error();
  char *digest = qa_digest_file(mount->archive_path);
  bool matches =
      digest != NULL && text_equal(digest, mount->archive_digest);
  free(digest);
  if (!matches) {
    qa_archive_source_destroy(&storage);
    plain_error("Archive bytes changed before mount: %s", mount->archive_path);
  }
  qa_mount_preparation_scope_assert_open(scope);
  qa_archive_handle *archive =
      qa_open_archive_source(&storage, true, mount->format);
  if (archive == NULL) oom_error();
  if (scope->count == scope->capacity) {
    size_t next = scope->capacity == 0 ? 4 : scope->capacity * 2;
    scope_archive *grown =
        (scope_archive *)realloc(scope->archives, next * sizeof *grown);
    if (grown == NULL) oom_error();
    scope->archives = grown;
    scope->capacity = next;
  }
  scope->archives[scope->count].path = copy_text_or_oom(mount->archive_path);
  scope->archives[scope->count].digest =
      copy_text_or_oom(mount->archive_digest);
  scope->archives[scope->count].format = mount->format;
  scope->archives[scope->count].archive = archive;
  scope->count++;
  return archive;
}
qa_content_mounted *qa_mount_preparation_scope_open(
    qa_mount_preparation_scope *scope, const qa_resolved_mount_plan *plan,
    const qa_open_mount_options *options) {
  qa_mount_preparation_scope_assert_open(scope);
  qa_content_mounted *mounted = mounted_create_plan(plan, options);
  if (mounted == NULL) return NULL;
  mounted_source *sources = (mounted_source *)calloc(
      plan->mount_count ? plan->mount_count : 1, sizeof *sources);
  if (sources == NULL) oom_error();
  for (size_t i = 0; i < plan->mount_count; i++) {
    const qa_content_mount *mount = &plan->mounts[i];
    sources[i].mount = *mount;
    sources[i].archive = NULL;
    if (mount->kind != QA_CONTENT_MOUNT_LOOSE) {
      qa_archive_handle *archive =
          scope_archive_for(scope, &mount->as.archive);
      qa_mount_preparation_scope_assert_open(scope);
      if (mounted->options.q3_restriction_demo)
        check_demo_archive(archive, &mount->as.archive, true);
      sources[i].archive = archive;
    }
  }
  qa_mount_preparation_scope_assert_open(scope);
  mounted->sources = sources;
  mounted->source_count = plan->mount_count;
  mounted->owner_kind = 2; /* OWNER_SCOPE */
  mounted->owner = scope;
  return mounted;
}
static qa_content_mounted *scope_opener_open(
    void *ctx, const qa_resolved_mount_plan *plan,
    const qa_open_mount_options *options) {
  return qa_mount_preparation_scope_open(
      (qa_mount_preparation_scope *)ctx, plan, options);
}
qa_mount_plan_opener qa_mount_preparation_scope_opener(
    qa_mount_preparation_scope *scope) {
  qa_mount_plan_opener opener = {scope, scope_opener_open};
  return opener;
}
void qa_mount_preparation_scope_dispose(qa_mount_preparation_scope *scope) {
  if (scope == NULL || scope->disposed) return;
  scope->closed = true;
  scope->disposed = true;
  for (size_t i = 0; i < scope->count; i++)
    qa_archive_close(scope->archives[i].archive);
}
void qa_mount_preparation_scope_destroy(qa_mount_preparation_scope *scope) {
  if (scope == NULL) return;
  qa_mount_preparation_scope_dispose(scope);
  for (size_t i = 0; i < scope->count; i++) {
    free(scope->archives[i].path);
    free(scope->archives[i].digest);
    qa_archive_destroy(scope->archives[i].archive);
  }
  free(scope->archives);
  free(scope);
}

