/* AddonLibrary (src/app/bootstrap/addon-library.ts) over local fixtures only, no network: the scenarios of
 * tests/app/addon-library-purge.test.ts and tests/content/catalog/addons.test.ts (purge at the next start,
 * local install/update/remove keeping prior loose bytes, a failed catalog publication restoring the previous
 * version, classic/rerelease independence, a game-directory ZIP, a non-ZIP and a missing file). Every status,
 * entry list and the resulting file tree (path + sha256) is compared line for line with the bun oracle
 * tests/app/oracles/addon_library.ts (addon_library.txt). Callbacks check a sentinel ctx.
 * Not reproduced: the TS "cancel during local import" ordering (stop() issued after submit() reaches the
 * import between two awaits; the C import runs inside submit()). GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>
#include "qa/app/bootstrap/addon_library.h"
#include "qa/content/catalog/addons.h"
#include "qa/network/common/session.h"

static int checks, failures;
#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)

typedef struct text { char *data; size_t length, capacity; } text;
static void out(text *t, const char *format, ...) {
  char line[4096];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof line, format, args);
  va_end(args);
  size_t n = strlen(line);
  if (t->length + n + 2 > t->capacity) {
    t->capacity = (t->capacity + n + 2) * 2;
    t->data = realloc(t->data, t->capacity);
    if (!t->data) abort();
  }
  memcpy(t->data + t->length, line, n);
  t->length += n;
  t->data[t->length++] = '\n';
  t->data[t->length] = '\0';
}
static char *replace_all(const char *source, const char *needle, const char *with) {
  size_t n = strlen(needle), w = strlen(with), count = 0;
  for (const char *p = source; n && (p = strstr(p, needle)) != NULL; p += n) count++;
  char *result = malloc(strlen(source) + count * (w + 1) + 1), *o = result;
  for (const char *p = source; *p;) {
    if (n && strncmp(p, needle, n) == 0) { memcpy(o, with, w); o += w; p += n; }
    else *o++ = *p++;
  }
  *o = '\0';
  return result;
}

/* The oracle's zip() (stored entries). */
static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, unsigned long v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void write_zip(const char *path, const char *(*entries)[2], size_t count) {
  uint8_t buffer[8192];
  memset(buffer, 0, sizeof buffer);
  size_t offset = 0, central_at, central_length = 0;
  unsigned long offsets[8], crcs[8];
  for (size_t i = 0; i < count; i++) {
    const char *name = entries[i][0], *data = entries[i][1];
    size_t nl = strlen(name), dl = strlen(data);
    crcs[i] = crc32(0, (const Bytef *)data, (uInt)dl);
    offsets[i] = offset;
    uint8_t *l = buffer + offset;
    put32(l, 0x04034b50); put16(l + 4, 20); put32(l + 14, crcs[i]); put32(l + 18, dl); put32(l + 22, dl); put16(l + 26, (unsigned)nl);
    memcpy(l + 30, name, nl); memcpy(l + 30 + nl, data, dl);
    offset += 30 + nl + dl;
  }
  central_at = offset;
  for (size_t i = 0; i < count; i++) {
    const char *name = entries[i][0], *data = entries[i][1];
    size_t nl = strlen(name), dl = strlen(data);
    uint8_t *c = buffer + central_at + central_length;
    put32(c, 0x02014b50); put16(c + 4, 20); put16(c + 6, 20); put32(c + 16, crcs[i]); put32(c + 20, dl); put32(c + 24, dl);
    put16(c + 28, (unsigned)nl); put32(c + 42, offsets[i]);
    memcpy(c + 46, name, nl);
    central_length += 46 + nl;
  }
  uint8_t *e = buffer + central_at + central_length;
  put32(e, 0x06054b50); put16(e + 8, (unsigned)count); put16(e + 10, (unsigned)count); put32(e + 12, central_length); put32(e + 16, central_at);
  FILE *file = fopen(path, "wb");
  fwrite(buffer, 1, central_at + central_length + 22, file);
  fclose(file);
}
static void write_text(const char *path, const char *value) {
  FILE *file = fopen(path, "wb");
  fputs(value, file);
  fclose(file);
}
static void mkdirs(const char *path) {
  char copy[2048];
  snprintf(copy, sizeof copy, "%s", path);
  for (char *p = copy + 1; *p; p++) if (*p == '/') { *p = '\0'; mkdir(copy, 0777); *p = '/'; }
  mkdir(copy, 0777);
}
static void remove_tree(const char *path) {
  struct stat info;
  if (lstat(path, &info) != 0) return;
  if (S_ISDIR(info.st_mode)) {
    DIR *dir = opendir(path);
    struct dirent *entry;
    while (dir && (entry = readdir(dir)) != NULL) {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
      char child[2048];
      snprintf(child, sizeof child, "%s/%s", path, entry->d_name);
      remove_tree(child);
    }
    if (dir) closedir(dir);
    rmdir(path);
  } else unlink(path);
}
static int compare_names(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
static void tree(text *t, const char *label, const char *root, const char *prefix) {
  char directory[2048];
  snprintf(directory, sizeof directory, "%s%s%s", root, prefix[0] ? "/" : "", prefix);
  DIR *dir = opendir(directory);
  if (!dir) return;
  char *names[256];
  size_t count = 0;
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL && count < 256)
    if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) names[count++] = strdup(entry->d_name);
  closedir(dir);
  qsort(names, count, sizeof *names, compare_names); /* JS sort of plain names: code unit order (ASCII here) */
  for (size_t i = 0; i < count; i++) {
    char relative[2048], full[4096];
    snprintf(relative, sizeof relative, "%s%s%s", prefix, prefix[0] ? "/" : "", names[i]);
    snprintf(full, sizeof full, "%s/%s", root, relative);
    struct stat info;
    stat(full, &info);
    if (strcmp(prefix, ".addons") == 0 && strncmp(names[i], "install-", 8) == 0) out(t, "%s tree .addons/install-* dir", label);
    else if (S_ISDIR(info.st_mode)) {
      out(t, "%s tree %s dir", label, relative);
      tree(t, label, root, relative);
    } else {
      FILE *file = fopen(full, "rb");
      uint8_t *bytes = malloc((size_t)info.st_size + 1);
      size_t read = fread(bytes, 1, (size_t)info.st_size, file);
      fclose(file);
      char hex[65];
      qa_net_sha256_hex(bytes, read, hex);
      free(bytes);
      out(t, "%s tree %s %s", label, relative, hex);
    }
    free(names[i]);
  }
}
static const char *current_root;
static void show(text *t, const char *name, qa_app_addon_library *library) {
  char *status = replace_all(qa_app_addon_library_status(library), current_root, "<root>");
  out(t, "%s status %s scope %s", name, status, qa_app_addon_library_scope(library));
  free(status);
  size_t count = 0;
  const qa_ui_library_entry *entries = qa_app_addon_library_entries(library, &count);
  for (size_t i = 0; i < count; i++)
    out(t, "%s entry %s|%s|%s|%s", name, entries[i].id, entries[i].label, entries[i].detail ? entries[i].detail : "-",
        entries[i].unavailable ? entries[i].unavailable : "-");
}

#define HOST_MAGIC 0xadd0
typedef struct host { int magic; int changes; bool fail; char launches[1024]; } host;
static host *host_of(void *ctx) { host *h = ctx; if (!h || h->magic != HOST_MAGIC) abort(); return h; }
static const char *h_changed(void *ctx) {
  host *h = host_of(ctx);
  if (h->fail) return "catalog failed";
  h->changes++;
  return NULL;
}
static void h_launch(void *ctx, const char *product, const char *map) {
  host *h = host_of(ctx);
  size_t used = strlen(h->launches);
  snprintf(h->launches + used, sizeof h->launches - used, "%s%s %s", used ? "," : "", product, map ? map : "null");
}
static qa_app_addon_library *open_library(const char *root, const char *edition, host *h) {
  qa_app_addon_library_options options;
  memset(&options, 0, sizeof options);
  options.root = root; options.edition = edition; options.ctx = h;
  options.changed = h_changed; options.launch = h_launch;
  return qa_app_addon_library_create(options);
}
static char *temp_root(const char *prefix) {
  char path[512];
  snprintf(path, sizeof path, "%s/%sXXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", prefix);
  char *made = mkdtemp(path);
  if (!made) abort();
  current_root = NULL;
  return strdup(made);
}
static const char *first_local(qa_app_addon_library *library) {
  static char id[256];
  size_t count = 0;
  const qa_ui_library_entry *entries = qa_app_addon_library_entries(library, &count);
  for (size_t i = 0; i < count; i++) if (strncmp(entries[i].id, "local:", 6) == 0) { snprintf(id, sizeof id, "%s", entries[i].id); return id; }
  return NULL;
}

static void run_scenarios(text *t) {
  char path[2048];
  { /* 1. purge */
    char *root = temp_root("quake-addon-purge-");
    current_root = root;
    const char *hidden = "qd_aaaaaaaaaaaaaaaaaaaa_bbbbbbbbbbbbbbbbbbbb", *kept = "qd_cccccccccccccccccccc_dddddddddddddddddddd";
    const char *digests[2] = {"eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee",
                              "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"};
    const char *names[2] = {hidden, kept};
    for (int i = 0; i < 2; i++) {
      snprintf(path, sizeof path, "%s/q1/%s", root, names[i]);
      mkdirs(path);
      char json[512];
      snprintf(json, sizeof json, "{\"sha256\":\"%s\",\"title\":\"%s\",\"group\":\"%s\",\"start\":null}", digests[i], names[i], names[i]);
      snprintf(path, sizeof path, "%s/q1/%s/.quaddicted.json", root, names[i]);
      write_text(path, json);
      snprintf(path, sizeof path, "%s/.addons", root);
      mkdirs(path);
      snprintf(path, sizeof path, "%s/.addons/%s.zip", root, digests[i]);
      write_text(path, "package");
    }
    snprintf(path, sizeof path, "%s/.addons/removed/q1", root);
    mkdirs(path);
    snprintf(path, sizeof path, "%s/.addons/removed/q1/%s", root, hidden);
    write_text(path, "");
    host h = {HOST_MAGIC, 0, false, ""};
    qa_app_addon_library *library = open_library(root, NULL, &h);
    qa_app_addon_library_read_installed(library);
    show(t, "purge", library);
    tree(t, "purge", root, "");
    qa_app_addon_library_close(library);
    qa_app_addon_library_destroy(library);
    remove_tree(root); free(root);
  }
  { /* 2. local install/update/remove */
    char *root = temp_root("addon-library-");
    current_root = root;
    host h = {HOST_MAGIC, 0, false, ""};
    qa_app_addon_library *library = open_library(root, NULL, &h);
    snprintf(path, sizeof path, "%s/map.zip", root);
    const char *first[][2] = {{"maps/example.bsp", "first version"}};
    write_zip(path, first, 1);
    qa_app_addon_library_create_submit(library, path);
    qa_app_addon_library_settle(library);
    show(t, "local-first", library);
    const char *second[][2] = {{"maps/example.bsp", "second version"}};
    write_zip(path, second, 1);
    qa_app_addon_library_create_submit(library, path);
    qa_app_addon_library_settle(library);
    out(t, "local changes %d", h.changes);
    show(t, "local-second", library);
    /* (await readdir(join(root, "q1"))).sort()[0] */
    char q1[2048];
    snprintf(q1, sizeof q1, "%s/q1", root);
    DIR *dir = opendir(q1);
    char lowest[256] = "";
    struct dirent *entry;
    while (dir && (entry = readdir(dir)) != NULL)
      if (entry->d_name[0] != '.' && (lowest[0] == '\0' || strcmp(entry->d_name, lowest) < 0)) snprintf(lowest, sizeof lowest, "%s", entry->d_name);
    if (dir) closedir(dir);
    char relative[512];
    snprintf(relative, sizeof relative, "q1/%s", lowest);
    out(t, "local first-hidden %s", qa_managed_addon_hidden(root, relative) ? "true" : "false");
    const char *local = first_local(library);
    if (local) {
      qa_app_addon_library_activate(library, local);
      show(t, "local-selected", library);
      qa_app_addon_library_activate(library, "local-remove");
      qa_app_addon_library_settle(library);
    }
    out(t, "local changes %d", h.changes);
    show(t, "local-removed", library);
    tree(t, "local", root, "");
    qa_app_addon_library_close(library);
    qa_app_addon_library_destroy(library);
    remove_tree(root); free(root);
  }
  { /* 3. failed catalog publication restores the previous version */
    char *root = temp_root("addon-cancel-");
    current_root = root;
    host h = {HOST_MAGIC, 0, false, ""};
    qa_app_addon_library *library = open_library(root, NULL, &h);
    snprintf(path, sizeof path, "%s/map.zip", root);
    const char *first[][2] = {{"maps/example.bsp", "first version"}};
    write_zip(path, first, 1);
    qa_app_addon_library_create_submit(library, path);
    qa_app_addon_library_settle(library);
    char original[256] = "";
    const char *local = first_local(library);
    if (local) snprintf(original, sizeof original, "%s", local);
    h.fail = true;
    const char *second[][2] = {{"maps/example.bsp", "second version"}};
    write_zip(path, second, 1);
    qa_app_addon_library_create_submit(library, path);
    qa_app_addon_library_settle(library);
    show(t, "restore", library);
    char relative[512];
    snprintf(relative, sizeof relative, "q1/%s", original[0] ? original + 6 : "");
    out(t, "restore original-hidden %s", qa_managed_addon_hidden(root, relative) ? "true" : "false");
    tree(t, "restore", root, "");
    qa_app_addon_library_close(library);
    qa_app_addon_library_destroy(library);
    remove_tree(root); free(root);
  }
  { /* 4. editions, a game-directory ZIP, a non-ZIP and a missing file */
    char *root = temp_root("addon-editions-");
    current_root = root;
    host h = {HOST_MAGIC, 0, false, ""}, r = {HOST_MAGIC, 0, false, ""};
    qa_app_addon_library *classic = open_library(root, "classic", &h);
    qa_app_addon_library *rerelease = open_library(root, "rerelease", &r);
    snprintf(path, sizeof path, "%s/Map.zip", root);
    const char *map[][2] = {{"maps/example.bsp", "geometry"}};
    write_zip(path, map, 1);
    qa_app_addon_library_create_submit(classic, path);
    qa_app_addon_library_settle(classic);
    qa_app_addon_library_create_submit(rerelease, path);
    qa_app_addon_library_settle(rerelease);
    show(t, "classic", classic);
    show(t, "rerelease", rerelease);
    size_t count = 0;
    const qa_ui_library_entry *entries = qa_app_addon_library_entries(classic, &count);
    if (count > 0) {
      char id[256];
      snprintf(id, sizeof id, "%s", entries[0].id);
      qa_app_addon_library_activate(classic, id);
      qa_app_addon_library_activate(classic, "local-play");
      qa_app_addon_library_settle(classic);
      qa_app_addon_library_activate(classic, "local-remove");
      qa_app_addon_library_settle(classic);
    }
    out(t, "editions launches %s", h.launches);
    show(t, "classic-removed", classic);
    snprintf(path, sizeof path, "%s/Game Pack.ZIP", root);
    const char *game[][2] = {{"mygame/maps/start.bsp", "start"}, {"mygame/progs.dat", "progs"}};
    write_zip(path, game, 2);
    qa_app_addon_library_create_submit(classic, path);
    qa_app_addon_library_settle(classic);
    show(t, "classic-game", classic);
    snprintf(path, sizeof path, "%s/notes.txt", root);
    write_text(path, "not a zip");
    qa_app_addon_library_create_submit(classic, path);
    qa_app_addon_library_settle(classic);
    show(t, "classic-text", classic);
    snprintf(path, sizeof path, "%s/missing.zip", root);
    qa_app_addon_library_create_submit(classic, path);
    qa_app_addon_library_settle(classic);
    char *status = replace_all(qa_app_addon_library_status(classic), root, "<root>");
    out(t, "classic-missing status %s", status);
    free(status);
    tree(t, "editions", root, "");
    qa_app_addon_library_close(classic);
    qa_app_addon_library_close(rerelease);
    qa_app_addon_library_destroy(classic);
    qa_app_addon_library_destroy(rerelease);
    remove_tree(root); free(root);
  }
}

int main(void) {
  text t = {0};
  run_scenarios(&t);
  FILE *file = fopen("tests/app/oracles/addon_library.txt", "rb");
  CHECK(file != NULL);
  if (!file) return 1;
  char *expected = calloc(1, 1 << 20);
  size_t read = fread(expected, 1, (1 << 20) - 1, file);
  expected[read] = '\0';
  fclose(file);
  const char *a = t.data ? t.data : "", *e = expected;
  size_t line = 1;
  while (*a || *e) {
    const char *an = strchr(a, '\n'), *en = strchr(e, '\n');
    size_t al = an ? (size_t)(an - a) : strlen(a), el = en ? (size_t)(en - e) : strlen(e);
    checks++;
    if (al != el || memcmp(a, e, al) != 0) {
      failures++;
      fprintf(stderr, "FAIL line %zu:\n  C:  %.*s\n  TS: %.*s\n", line, (int)al, a, (int)el, e);
    }
    a += al + (an ? 1 : 0); e += el + (en ? 1 : 0); line++;
  }
  printf("test_addon_library: %d checks, %d failures\n", checks, failures);
  free(expected); free(t.data);
  return failures != 0;
}
