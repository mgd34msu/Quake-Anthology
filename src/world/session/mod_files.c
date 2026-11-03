/* Port of src/world/session/mod-files.ts. GPL-2.0-or-later. */
#include "qa/world/session/mod_files.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void *xrealloc(void *ptr, size_t size) {
  void *next = realloc(ptr, size != 0 ? size : 1);
  if (next == NULL) abort();
  return next;
}
static char *xstrdup(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  if (copy == NULL) abort();
  memcpy(copy, text, length);
  return copy;
}

/* JS encodeURIComponent over the string's UTF-8 bytes. */
static char *encode_component(const char *text) {
  static const char hex[] = "0123456789ABCDEF";
  size_t length = strlen(text);
  char *out = malloc(length * 3 + 1);
  if (out == NULL) abort();
  size_t used = 0;
  for (size_t i = 0; i < length; i++) {
    unsigned char c = (unsigned char)text[i];
    bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                      (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                      c == '.' || c == '!' || c == '~' || c == '*' ||
                      c == '\'' || c == '(' || c == ')';
    if (unreserved) {
      out[used++] = (char)c;
    } else {
      out[used++] = '%';
      out[used++] = hex[c >> 4];
      out[used++] = hex[c & 15];
    }
  }
  out[used] = '\0';
  return out;
}

/* Node path.resolve (POSIX) over segments: right to left until a segment
 * is absolute, then the working directory; "." segments drop, ".." pops
 * (never above "/"), empty segments and repeated or trailing slashes
 * vanish. "." and ".." survive encodeURIComponent, so a selection component
 * can climb out of ".mods" exactly as in TS. */
static char *node_path_resolve(const char *const *segments, size_t count) {
  size_t start = 0;
  bool absolute = false;
  for (size_t i = count; i-- > 0;)
    if (segments[i][0] == '/') {
      start = i;
      absolute = true;
      break;
    }
  char *cwd = NULL;
  if (!absolute) {
    cwd = getcwd(NULL, 0); /* process.cwd() */
    if (cwd == NULL) abort();
  }
  size_t total = (cwd != NULL ? strlen(cwd) : 0) + 2;
  for (size_t i = start; i < count; i++) total += strlen(segments[i]) + 1;
  char *out = malloc(total);
  if (out == NULL) abort();
  size_t used = 0;
  out[used++] = '/';
  /* The texts to join: the working directory when nothing is absolute. */
  size_t parts = count - start + (cwd != NULL ? 1 : 0);
  for (size_t part = 0; part < parts; part++) {
    const char *text = cwd != NULL ? (part == 0 ? cwd : segments[start + part - 1])
                                   : segments[start + part];
    const char *cursor = text;
    while (*cursor != '\0') {
      while (*cursor == '/') cursor++;
      const char *end = cursor;
      while (*end != '\0' && *end != '/') end++;
      size_t length = (size_t)(end - cursor);
      if (length == 0 || (length == 1 && cursor[0] == '.')) {
        /* skip */
      } else if (length == 2 && cursor[0] == '.' && cursor[1] == '.') {
        while (used > 1 && out[used - 1] != '/') used--;
        if (used > 1) used--; /* the separator before the popped name */
      } else {
        if (used > 1) out[used++] = '/';
        memcpy(out + used, cursor, length);
        used += length;
      }
      cursor = end;
    }
  }
  out[used] = '\0';
  free(cwd);
  return out;
}

struct store_entry {
  char *key; /* owned */
  qa_user_file_store *store; /* owned; returned pointers stay valid */
};
struct qa_mod_user_files {
  char *root; /* owned */
  struct store_entry *stores;
  size_t count;
  size_t capacity;
};

qa_mod_user_files *qa_mod_user_files_create(const char *root) {
  qa_mod_user_files *files = calloc(1, sizeof *files);
  if (files == NULL) return NULL;
  files->root = xstrdup(root);
  return files;
}
void qa_mod_user_files_destroy(qa_mod_user_files *files) {
  if (files == NULL) return;
  for (size_t i = 0; i < files->count; i++) {
    free(files->stores[i].key);
    qa_user_file_store_destroy(files->stores[i].store);
    free(files->stores[i].store);
  }
  free(files->stores);
  free(files->root);
  free(files);
}
const char *qa_mod_user_files_root(const qa_mod_user_files *files) {
  return files->root;
}

qa_user_file_store *qa_mod_user_files_for(qa_mod_user_files *files,
                                          qa_mod_selection selection) {
  char *key = qa_mod_selection_key(selection);
  if (key == NULL) abort();
  for (size_t i = 0; i < files->count; i++)
    if (strcmp(files->stores[i].key, key) == 0) {
      free(key);
      return files->stores[i].store;
    }
  char *product = encode_component(selection.product);
  char *id = encode_component(selection.id);
  const char *segments[4] = {files->root, ".mods", product, id};
  char *path = node_path_resolve(segments, 4);
  free(product);
  free(id);
  if (files->count == files->capacity) {
    files->capacity = files->capacity != 0 ? files->capacity * 2 : 8;
    files->stores =
        xrealloc(files->stores, files->capacity * sizeof *files->stores);
  }
  struct store_entry *entry = &files->stores[files->count++];
  entry->key = key;
  entry->store = malloc(sizeof *entry->store);
  if (entry->store == NULL || !qa_user_file_store_init(entry->store, path)) abort();
  free(path);
  return entry->store;
}

/* sha256 hex (the createHash digest) via the mounter's digest, whose
 * `sha256:` prefix is stripped. */
static char *sha256_hex(const char *text, size_t length) {
  char *digest = qa_digest_bytes((const uint8_t *)text, length);
  if (digest == NULL) abort();
  const char *hex = strncmp(digest, "sha256:", 7) == 0 ? digest + 7 : digest;
  char *copy = xstrdup(hex);
  free(digest);
  return copy;
}

qa_content_mounted *qa_borrow_mod_file_mounts(
    qa_mod_selection selection, qa_content_id content,
    qa_content_mounted *installed, qa_user_file_store *writable) {
  char *key = qa_mod_selection_key(selection);
  if (key == NULL) abort();
  char *identity = sha256_hex(key, strlen(key));
  /* Identity strings outlive this call inside the mounted plan. */
  char *mount_id = qa_create_mount_id("mod-user", identity);
  free(identity);
  if (mount_id == NULL) abort();
  qa_loose_mount mount;
  memset(&mount, 0, sizeof mount);
  mount.root_path = writable->root;
  mount.identity = qa_create_mount_identity(mount_id, content, 0);
  const char *installed_id =
      installed != NULL ? qa_content_mounted_plan(installed)->id : "";
  size_t plan_source_length = strlen(key) + 1 + strlen(installed_id);
  char *plan_source = malloc(plan_source_length + 1);
  if (plan_source == NULL) abort();
  memcpy(plan_source, key, strlen(key));
  plan_source[strlen(key)] = '\0';
  memcpy(plan_source + strlen(key) + 1, installed_id,
         strlen(installed_id) + 1);
  char *plan_digest = sha256_hex(plan_source, plan_source_length);
  free(plan_source);
  char *plan = qa_create_mount_plan_id("mod-files", plan_digest);
  free(plan_digest);
  free(key);
  if (plan == NULL) abort();
  if (installed != NULL)
    return qa_content_mounted_borrow_with_loose_mount(installed, mount,
                                                      plan);
  qa_content_mount mounts[1];
  memset(mounts, 0, sizeof mounts);
  mounts[0].kind = QA_CONTENT_MOUNT_LOOSE;
  mounts[0].as.loose = mount;
  qa_mount_id order[1] = {mount.identity.id};
  qa_resolved_mount_plan resolved;
  memset(&resolved, 0, sizeof resolved);
  resolved.id = plan;
  resolved.mounts = mounts;
  resolved.mount_count = 1;
  resolved.default_order = order;
  resolved.default_order_count = 1;
  qa_open_mount_options options;
  memset(&options, 0, sizeof options);
  return qa_open_mount_plan(&resolved, &options);
}
