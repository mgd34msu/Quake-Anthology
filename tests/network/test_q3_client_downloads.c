/*
 * Port of ../quake-anthology-ts/tests/network/q3/client-downloads.test.ts over
 * qa_app_q3_client_downloads, the native Q3 server download source and the
 * shared download sink. Temporary roots live under the build-local tmp
 * directory and are removed on every path. TS async gates become synchronous
 * callbacks performing the same mutation at the resumption point.
 * GPL-2.0-or-later.
 */
#define _XOPEN_SOURCE 700
#include <dirent.h>
#include <ftw.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

#include "qa/app/bootstrap/network/client_download_policy.h"
#include "qa/app/bootstrap/network/q3_client_downloads.h"
#include "qa/content/archive.h"
#include "qa/foundation/support.h"
#include "qa/network/q3.h"
#include "qa/network/services/downloads.h"

static int failures = 0;
static int checks = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)
#define MAGIC 0x41c8e0d3u
static void *sentinel(void *ctx) {
  CHECK(ctx != NULL && *(unsigned *)ctx == MAGIC);
  return ctx;
}
#define TMP_BASE "/home/buzzkill/Projects/quake-anthology-c/build/agents/network2-q3/tmp"

/* ---- filesystem helpers ---- */
static int remove_entry(const char *path, const struct stat *sb, int flag, struct FTW *ftw) {
  (void)sb; (void)flag; (void)ftw;
  return remove(path);
}
static void remove_tree(const char *root) { nftw(root, remove_entry, 16, FTW_DEPTH | FTW_PHYS); }
static char *make_root(const char *prefix) {
  mkdir(TMP_BASE, 0755);
  char *path = malloc(512);
  snprintf(path, 512, "%s/%sXXXXXX", TMP_BASE, prefix);
  CHECK(mkdtemp(path) != NULL);
  return path;
}
static char *join(const char *a, const char *b) {
  size_t n = strlen(a) + strlen(b) + 2;
  char *out = malloc(n);
  snprintf(out, n, "%s/%s", a, b);
  return out;
}
/* Entry names of a directory, sorted; returns count (-1 when missing). */
static int list_dir(const char *path, char names[8][128]) {
  DIR *dir = opendir(path);
  if (!dir) return -1;
  int count = 0;
  struct dirent *e;
  while ((e = readdir(dir))) {
    if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
    if (count < 8) snprintf(names[count], 128, "%.127s", e->d_name);
    count++;
  }
  closedir(dir);
  for (int i = 0; i < count && i < 8; i++)
    for (int j = i + 1; j < count && j < 8; j++)
      if (strcmp(names[j], names[i]) < 0) { char t[128]; memcpy(t, names[i], 128); memcpy(names[i], names[j], 128); memcpy(names[j], t, 128); }
  return count;
}
static bool dir_is(const char *path, const char *only) {
  char names[8][128];
  int n = list_dir(path, names);
  if (only == NULL) return n == 0;
  return n == 1 && strcmp(names[0], only) == 0;
}
static uint8_t *read_file(const char *path, size_t *len) {
  FILE *file = fopen(path, "rb");
  if (!file) return NULL;
  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  fseek(file, 0, SEEK_SET);
  uint8_t *bytes = malloc(size > 0 ? (size_t)size : 1);
  *len = fread(bytes, 1, (size_t)size, file);
  fclose(file);
  return bytes;
}
static void write_file(const char *path, const char *text) {
  FILE *file = fopen(path, "wb");
  CHECK(file != NULL);
  if (file) { fputs(text, file); fclose(file); }
}
static bool file_equals(const char *path, const uint8_t *bytes, size_t len) {
  size_t got = 0;
  uint8_t *data = read_file(path, &got);
  bool same = data && got == len && memcmp(data, bytes, len) == 0;
  free(data);
  return same;
}

/* ---- TS fixturePk3 ---- */
static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint8_t *fixture_pk3(size_t *out_len) {
  static const char name[] = "wire-probe.cfg";
  const size_t name_len = sizeof name - 1;
  static const char line[] = "set wire_probe 1\n";
  const size_t data_len = (sizeof line - 1) * 400;
  uint8_t *data = malloc(data_len);
  for (size_t i = 0; i < 400; i++) memcpy(data + i * (sizeof line - 1), line, sizeof line - 1);
  size_t local = 30 + name_len + data_len, central = 46 + name_len, total = local + central + 22;
  uint8_t *bytes = calloc(total, 1);
  uint32_t crc = (uint32_t)crc32(0L, data, (uInt)data_len);
  put32(bytes, 0x04034b50); put16(bytes + 4, 20); put32(bytes + 14, crc);
  put32(bytes + 18, (uint32_t)data_len); put32(bytes + 22, (uint32_t)data_len); put16(bytes + 26, (uint32_t)name_len);
  memcpy(bytes + 30, name, name_len); memcpy(bytes + 30 + name_len, data, data_len);
  put32(bytes + local, 0x02014b50); put16(bytes + local + 4, 20); put16(bytes + local + 6, 20);
  put32(bytes + local + 16, crc); put32(bytes + local + 20, (uint32_t)data_len); put32(bytes + local + 24, (uint32_t)data_len);
  put16(bytes + local + 28, (uint32_t)name_len); memcpy(bytes + local + 46, name, name_len);
  size_t end = local + central;
  put32(bytes + end, 0x06054b50); put16(bytes + end + 8, 1); put16(bytes + end + 10, 1);
  put32(bytes + end + 12, (uint32_t)central); put32(bytes + end + 16, (uint32_t)local);
  free(data);
  *out_len = total;
  return bytes;
}

/* ---- fixture ---- */
typedef struct fixture {
  unsigned magic;
  char *root;
  uint8_t *bytes; size_t len;
  int64_t checksum;
  char commands[32][96]; size_t command_count;
  char events[16][16]; size_t event_count;
  bool close_on_reload;
  qa_app_q3_client_downloads *client;
  qa_q3_server_download *server;
  size_t position;
  qa_app_q3_download_mounts owner; char *owner_root; bool has_owner;
  qa_q3_server_message message; bool has_message;
} fixture;
static fixture *F(void *ctx) { return sentinel(ctx); }
static void b_assert(void *ctx) { sentinel(ctx); }
static void b_reliable(void *ctx, const char *text) { fixture *f = F(ctx); if (f->command_count < 32) snprintf(f->commands[f->command_count++], 96, "%.95s", text); }
static void b_packet(void *ctx) { fixture *f = F(ctx); if (f->event_count < 16) snprintf(f->events[f->event_count++], 16, "packet"); }
static void b_progress(void *ctx, const char *name, double count, double size) { sentinel(ctx); (void)name; (void)count; (void)size; }
static void b_reload(void *ctx) {
  fixture *f = F(ctx);
  if (f->event_count < 16) snprintf(f->events[f->event_count++], 16, "reload");
  if (f->close_on_reload) qa_app_q3_client_downloads_close(f->client); /* retire the epoch during the refresh */
}
static bool b_exists(void *ctx, const char *path) {
  fixture *f = F(ctx);
  char *full = join(f->root, path);
  struct stat st;
  bool exists = stat(full, &st) == 0;
  free(full);
  return exists;
}
static int64_t s_read(void *ctx, uint8_t *target, size_t capacity) {
  fixture *f = F(ctx);
  size_t count = f->len - f->position < capacity ? f->len - f->position : capacity;
  memcpy(target, f->bytes + f->position, count);
  f->position += count;
  return (int64_t)count;
}
static void s_close(void *ctx) { sentinel(ctx); }
static bool s_open(void *ctx, const char *name, qa_q3_download_read_file *out) {
  fixture *f = F(ctx);
  (void)name;
  out->size = (int64_t)f->len; out->read = s_read; out->close = s_close; out->ctx = f;
  return true;
}
static bool s_true(void *ctx) { sentinel(ctx); return true; }
static bool s_false(void *ctx) { sentinel(ctx); return false; }
static void s_drop(void *ctx, const char *reason) { sentinel(ctx); fprintf(stderr, "drop: %s\n", reason); CHECK(!"unexpected server drop"); }
static void s_print(void *ctx, const char *text) { sentinel(ctx); (void)text; }
static fixture *make_fixture(int64_t checksum_delta, bool earlier_loaded, bool close_on_reload, const char *physical_directory) {
  fixture *f = calloc(1, sizeof *f);
  f->magic = MAGIC;
  f->root = make_root("q3-download-sink-");
  f->bytes = fixture_pk3(&f->len);
  qa_archive_handle *archive = qa_decode_archive(f->bytes, f->len, true, QA_ARCHIVE_FORMAT_PK3, NULL);
  CHECK(archive != NULL);
  int64_t checksum = 0, pure = 0;
  qa_q3_archive_checksums(archive, 0, &checksum, &pure);
  qa_archive_destroy(archive);
  f->checksum = checksum ^ checksum_delta;
  f->close_on_reload = close_on_reload;
  if (physical_directory) {
    f->has_owner = true;
    f->owner_root = join(f->root, physical_directory);
    f->owner.selection_base = "q3-baseq3"; f->owner.selection_directory = "";
    f->owner.write_root = f->owner_root; f->owner.base_write_root = f->owner_root;
    CHECK(mkdir(f->owner_root, 0755) == 0);
    char *existing = join(f->owner_root, "custom.pk3");
    write_file(existing, "existing package");
    free(existing);
  }
  qa_app_q3_download_bindings bindings;
  memset(&bindings, 0, sizeof bindings);
  bindings.ctx = f; bindings.assert_current = b_assert; bindings.reliable = b_reliable; bindings.send_packet = b_packet;
  bindings.progress = b_progress; bindings.reload_packages = b_reload;
  f->client = qa_app_q3_client_downloads_create(f->root, bindings, f->has_owner ? &f->owner : NULL);
  CHECK(f->client != NULL);
  qa_q3_server_pak referenced[2];
  size_t referenced_count = 0;
  if (earlier_loaded) { referenced[referenced_count].name = "baseq3/custom"; referenced[referenced_count++].checksum = f->checksum ^ 1; }
  referenced[referenced_count].name = "baseq3/custom"; referenced[referenced_count++].checksum = f->checksum;
  double loaded[1] = { (double)(f->checksum ^ 1) };
  qa_app_q3_client_downloads_begin(f->client, referenced, referenced_count, loaded, earlier_loaded ? 1 : 0, b_exists, f);
  qa_q3_download_server_bindings server;
  memset(&server, 0, sizeof server);
  server.open = s_open; server.enabled = s_true; server.pure = s_false; server.drop = s_drop; server.print = s_print; server.print_ctx = f; server.ctx = f;
  CHECK(qa_q3_server_download_init(&f->server, &server));
  qa_q3_server_download_begin(f->server, "baseq3/custom.pk3");
  return f;
}
static void destroy_fixture(fixture *f) {
  qa_app_q3_client_downloads_close(f->client);
  qa_app_q3_client_downloads_destroy(f->client);
  qa_q3_server_download_close(f->server);
  qa_q3_server_download_destroy(f->server);
  if (f->has_message) qa_q3_server_message_destroy(&f->message);
  remove_tree(f->root);
  free(f->root); free(f->owner_root); free(f->bytes);
  free(f);
}
/* TS blocks(time): one server snapshot-carrier message decoded into its download blocks. */
static size_t blocks(fixture *f, double time, const qa_q3_download_block **out, size_t capacity) {
  if (f->has_message) { qa_q3_server_message_destroy(&f->message); f->has_message = false; }
  qa_q3_message_writer *writer = NULL;
  CHECK(qa_q3_message_writer_init_default(&writer));
  qa_q3_message_writer_write_long(writer, 0);
  qa_q3_download_rate rate = { 1000000, 0, 50 };
  qa_q3_server_download_write(f->server, writer, time, &rate);
  qa_q3_message_writer_write_byte(writer, QA_Q3_SVC_EOF);
  uint8_t *bytes = NULL; size_t len = 0;
  CHECK(qa_q3_message_writer_to_bytes(writer, &bytes, &len));
  qa_q3_message_writer_destroy(writer);
  qa_q3_server_message_context context;
  memset(&context, 0, sizeof context);
  context.product = QA_Q3_PRODUCT_BASEQ3; context.message_number = 1;
  CHECK(qa_q3_decode_server_message(bytes, len, &context, NULL, &f->message));
  free(bytes);
  f->has_message = true;
  size_t count = 0;
  for (size_t i = 0; i < f->message.operation_count; i++)
    if (f->message.operations[i].kind == QA_Q3_OP_DOWNLOAD && count < capacity) out[count++] = &f->message.operations[i].download;
  return count;
}
static void deliver(fixture *f, const qa_q3_download_block *block) {
  if (block->kind == QA_Q3_DOWNLOAD_START) qa_app_q3_client_downloads_publish_size(f->client, (double)block->file_size);
  qa_app_q3_client_downloads_receive(f->client, block);
}
typedef struct deliver_scope { fixture *f; const qa_q3_download_block *const *blocks; size_t count; } deliver_scope;
static void deliver_all_body(void *ctx) {
  deliver_scope *s = ctx;
  for (size_t i = 0; i < s->count; i++) deliver(s->f, s->blocks[i]);
}
static bool has_command(fixture *f, const char *text) {
  for (size_t i = 0; i < f->command_count; i++) if (strcmp(f->commands[i], text) == 0) return true;
  return false;
}
static const char *last_command(fixture *f) { return f->command_count ? f->commands[f->command_count - 1] : ""; }

static void test_directory_mapping(void) {
  fixture *f = make_fixture(0, false, false, "BaseQ3");
  const qa_q3_download_block *list[16];
  size_t n = blocks(f, 2000, list, 16);
  for (size_t i = 0; i < n; i++) deliver(f, list[i]);
  char *existing = join(f->root, "BaseQ3/custom.pk3");
  CHECK(file_equals(existing, (const uint8_t *)"existing package", 16));
  free(existing);
  char *base = join(f->root, "BaseQ3");
  char names[8][128];
  CHECK(list_dir(base, names) == 2);
  free(base);
  char *lower = join(f->root, "baseq3");
  struct stat st;
  CHECK(stat(lower, &st) != 0);
  free(lower);
  CHECK(f->command_count >= 1 && strcmp(f->commands[0], "download baseq3/custom.pk3") == 0);
  CHECK(strcmp(last_command(f), "donedl") == 0);
  char *mymod = join(f->root, "MyMod"), *baseq3 = join(f->root, "BaseQ3");
  qa_app_q3_download_mounts owner = { "q3-baseq3", "mymod", mymod, baseq3 };
  char *path = qa_app_q3_download_path("MYMOD/custom.123.pk3", &owner);
  CHECK(path && strcmp(path, "MyMod/custom.123.pk3") == 0); free(path);
  path = qa_app_q3_download_path("BASEQ3/custom.pk3", &owner);
  CHECK(path && strcmp(path, "BaseQ3/custom.pk3") == 0); free(path);
  path = qa_app_q3_download_path("other/custom.pk3", &owner);
  CHECK(path && strcmp(path, "other/custom.pk3") == 0); free(path);
  free(mymod); free(baseq3);
  destroy_fixture(f);
}
typedef struct path_scope { const qa_app_q3_download_mounts *owner; } path_scope;
static void traversal_body(void *ctx) { free(qa_app_q3_download_path("../custom.pk3", ((path_scope *)ctx)->owner)); }
static void test_directory_traversal(void) {
  qa_app_q3_download_mounts owner = { "q3-baseq3", "mymod", TMP_BASE "/MyMod", TMP_BASE "/BaseQ3" };
  path_scope scope = { &owner };
  qa_caught_error caught;
  CHECK(!qa_try(traversal_body, &scope, &caught));
}

static void test_block_retry_and_publish(void) {
  fixture *f = make_fixture(0, false, false, NULL);
  const qa_q3_download_block *list[16];
  size_t n = blocks(f, 2000, list, 16);
  CHECK(n >= 1 && list[0]->kind == QA_Q3_DOWNLOAD_START);
  if (n >= 1) deliver(f, list[0]);
  char *installed = join(f->root, "baseq3/custom.pk3");
  struct stat st;
  CHECK(stat(installed, &st) != 0);
  n = blocks(f, 3101, list, 16); /* the source resend includes the accepted first block */
  for (size_t i = 0; i < n; i++) deliver(f, list[i]);
  CHECK(file_equals(installed, f->bytes, f->len));
  size_t first_acks = 0;
  for (size_t i = 0; i < f->command_count; i++) if (strcmp(f->commands[i], "nextdl 0") == 0) first_acks++;
  CHECK(first_acks == 1);
  CHECK(strcmp(last_command(f), "donedl") == 0);
  CHECK(f->event_count == 3 && strcmp(f->events[0], "packet") == 0 && strcmp(f->events[1], "packet") == 0 && strcmp(f->events[2], "reload") == 0);
  char *base = join(f->root, "baseq3");
  CHECK(dir_is(base, "custom.pk3"));
  free(base);
  for (size_t i = 0; i < f->command_count; i++)
    if (strncmp(f->commands[i], "nextdl ", 7) == 0) qa_q3_server_download_acknowledge(f->server, atoi(f->commands[i] + 7), 3200);
  CHECK(strcmp(qa_q3_server_download_name(f->server), "") == 0);
  free(installed);
  destroy_fixture(f);
}

static void test_wrong_checksum_and_cancel(void) {
  for (int wrong = 0; wrong < 2; wrong++) {
    fixture *f = make_fixture(wrong ? 1 : 0, false, false, NULL);
    const qa_q3_download_block *list[16];
    size_t n = blocks(f, 2000, list, 16);
    if (!wrong) {
      CHECK(n >= 1 && list[0]->kind == QA_Q3_DOWNLOAD_START);
      if (n >= 1) deliver(f, list[0]);
      qa_app_q3_client_downloads_close(f->client);
    } else {
      deliver_scope scope = { f, list, n };
      qa_caught_error caught;
      CHECK(!qa_try(deliver_all_body, &scope, &caught) && strstr(caught.message, "checksum") != NULL);
    }
    char *base = join(f->root, "baseq3");
    CHECK(dir_is(base, NULL));
    free(base);
    CHECK(!has_command(f, "donedl"));
    destroy_fixture(f);
  }
}

typedef struct inspect_fixture { unsigned magic; qa_download_sink *sink; char *root; bool append_rejected, finish_rejected, read_ok; } inspect_fixture;
static void append_body(void *ctx) { qa_download_sink_append(((inspect_fixture *)ctx)->sink, (const uint8_t *)"", 0); }
static void finish_body(void *ctx) { free(qa_download_sink_finish(((inspect_fixture *)ctx)->sink)); }
static void inspect(void *ctx, const char *path) {
  inspect_fixture *f = sentinel(ctx);
  qa_caught_error caught;
  f->append_rejected = !qa_try(append_body, f, &caught) && strstr(caught.message, "inspection") != NULL;
  f->finish_rejected = !qa_try(finish_body, f, &caught) && strstr(caught.message, "inspection") != NULL;
  qa_download_sink_close(f->sink);
  char *other = join(f->root, "other");
  write_file(other, "unrelated");
  free(other);
  static const uint8_t expected[3] = { 1, 2, 3 };
  f->read_ok = file_equals(path, expected, 3);
}
static void inspect_body(void *ctx) {
  inspect_fixture *f = ctx;
  qa_download_sink_inspect_staged(f->sink, inspect, f);
}
static void test_staged_inspection(void) {
  inspect_fixture f;
  memset(&f, 0, sizeof f);
  f.magic = MAGIC;
  f.root = make_root("download-inspect-");
  qa_download_sink sink;
  qa_download_expectation expected = { true, NULL, 0, 8 };
  CHECK(qa_download_sink_create(&sink, f.root, "test.pk3", expected));
  f.sink = &sink;
  static const uint8_t data[3] = { 1, 2, 3 };
  qa_download_sink_append(&sink, data, 3);
  qa_caught_error caught;
  CHECK(!qa_try(inspect_body, &f, &caught) && strstr(caught.message, "closed during inspection") != NULL);
  CHECK(f.append_rejected);
  CHECK(f.finish_rejected);
  CHECK(f.read_ok);
  CHECK(dir_is(f.root, "other"));
  qa_download_sink_close(&sink);
  remove_tree(f.root);
  free(f.root);
}

static void test_destination_installed_during_transfer(void) {
  fixture *f = make_fixture(0, false, false, NULL);
  const qa_q3_download_block *list[16];
  size_t n = blocks(f, 2000, list, 16);
  CHECK(n >= 1 && list[0]->kind == QA_Q3_DOWNLOAD_START);
  if (n >= 1) deliver(f, list[0]);
  char *path = join(f->root, "baseq3/custom.pk3");
  write_file(path, "installed");
  deliver_scope scope = { f, list + 1, n ? n - 1 : 0 };
  qa_caught_error caught;
  CHECK(!qa_try(deliver_all_body, &scope, &caught));
  CHECK(file_equals(path, (const uint8_t *)"installed", 9));
  char *base = join(f->root, "baseq3");
  CHECK(dir_is(base, "custom.pk3"));
  free(base); free(path);
  CHECK(!has_command(f, "donedl"));
  destroy_fixture(f);
}

static void test_loaded_same_name(void) {
  fixture *f = make_fixture(0, true, false, NULL);
  const qa_q3_download_block *list[16];
  size_t n = blocks(f, 2000, list, 16);
  for (size_t i = 0; i < n; i++) deliver(f, list[i]);
  char *path = join(f->root, "baseq3/custom.pk3");
  CHECK(file_equals(path, f->bytes, f->len));
  free(path);
  CHECK(strcmp(last_command(f), "donedl") == 0);
  destroy_fixture(f);
}

typedef struct block_scope { fixture *f; qa_q3_download_block block; } block_scope;
static void block_body(void *ctx) { block_scope *s = ctx; deliver(s->f, &s->block); }
static void test_stock_and_unsolicited(void) {
  fixture *f = make_fixture(0, false, false, NULL);
  qa_app_q3_client_downloads_close(f->client);
  f->command_count = 0;
  qa_q3_server_pak stock[2] = { { 1, "baseq3/pak0" }, { 2, "missionpack/pak8" } };
  CHECK(!qa_app_q3_client_downloads_begin(f->client, stock, 2, NULL, 0, b_exists, f));
  CHECK(f->command_count == 0);
  uint8_t one[1] = { 1 };
  qa_q3_download_block start;
  memset(&start, 0, sizeof start);
  start.kind = QA_Q3_DOWNLOAD_START; start.file_size = 10; start.data = one; start.data_len = 1;
  deliver(f, &start);
  CHECK(f->command_count == 1 && strcmp(f->commands[0], "stopdl") == 0);
  CHECK(dir_is(f->root, NULL));
  qa_q3_server_pak empty[1] = { { 0, "baseq3/empty" } };
  qa_app_q3_client_downloads_begin(f->client, empty, 1, NULL, 0, b_exists, f);
  block_scope scope;
  memset(&scope, 0, sizeof scope);
  scope.f = f; scope.block.kind = QA_Q3_DOWNLOAD_START; scope.block.file_size = 0;
  qa_caught_error caught;
  CHECK(!qa_try(block_body, &scope, &caught) && strstr(caught.message, "temporary request") != NULL);
  CHECK(dir_is(f->root, NULL));
  destroy_fixture(f);
}

static void test_retired_during_refresh(void) {
  fixture *f = make_fixture(0, false, true, NULL);
  const qa_q3_download_block *list[16];
  size_t n = blocks(f, 2000, list, 16);
  deliver_scope scope = { f, list, n };
  qa_caught_error caught;
  CHECK(!qa_try(deliver_all_body, &scope, &caught) && strstr(caught.message, "retired during filesystem refresh") != NULL);
  CHECK(!has_command(f, "donedl"));
  destroy_fixture(f);
}

static void test_cancel_and_retry(void) {
  fixture *f = make_fixture(0, false, false, NULL);
  qa_app_client_download_progress rows[4];
  CHECK(qa_app_q3_client_downloads_progress(f->client, rows, 4) >= 1 && rows[0].phase == QA_APP_DL_RUNNING);
  qa_app_q3_client_downloads_cancel(f->client);
  qa_app_q3_client_downloads_cancel(f->client);
  CHECK(strcmp(last_command(f), "stopdl") == 0);
  CHECK(qa_app_q3_client_downloads_progress(f->client, rows, 4) >= 1 && rows[0].phase == QA_APP_DL_PENDING);
  CHECK(qa_app_q3_client_downloads_retry(f->client));
  CHECK(strcmp(last_command(f), "download baseq3/custom.pk3") == 0);
  CHECK(qa_app_q3_client_downloads_progress(f->client, rows, 4) >= 1 && rows[0].received == 0);
  qa_app_q3_client_downloads_close(f->client);
  CHECK(!qa_app_q3_client_downloads_retry(f->client));
  CHECK(qa_app_q3_client_downloads_progress(f->client, rows, 4) == 0);
  CHECK(dir_is(f->root, NULL));
  destroy_fixture(f);
}

int main(void) {
  test_directory_mapping();
  test_directory_traversal();
  test_block_retry_and_publish();
  test_wrong_checksum_and_cancel();
  test_staged_inspection();
  test_destination_installed_during_transfer();
  test_loaded_same_name();
  test_stock_and_unsolicited();
  test_retired_during_refresh();
  test_cancel_and_retry();
  printf("test_q3_client_downloads: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
