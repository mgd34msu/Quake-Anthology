/*
 * Port of tests/content/catalog/archive.test.ts plus key cases from
 * tests/content/catalog/mounts.test.ts and the roots/paths/user-data
 * and declaration-reader surfaces. Corpus witness cases are skipped
 * (no ~ /qfiles in this environment; the TS skips them too when the
 * corpus is absent). TS toThrow cases run in a child and must abort.
 * GPL-2.0-or-later.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <zlib.h>

#include "qa/content/archive.h"
#include "qa/content/held_weapon.h"
#include "qa/content/item_icon.h"
#include "qa/content/model_attachment.h"
#include "qa/content/mounts.h"
#include "qa/content/roots.h"
#include "qa/content/user_data.h"
#include "qa/contracts/content.h"

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

#define CHECK_ABORTS(...)                                               \
  do {                                                                  \
    checks++;                                                           \
    pid_t pid = fork();                                                 \
    if (pid == 0) {                                                     \
      (void)prctl(PR_SET_DUMPABLE, 0); /* no coredump for expected aborts */ \
      freopen("/dev/null", "w", stderr);                                \
      __VA_ARGS__;                                                      \
      _exit(0);                                                         \
    }                                                                   \
    int status = 0;                                                     \
    waitpid(pid, &status, 0);                                           \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {        \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__,     \
              __LINE__, #__VA_ARGS__);                                  \
    }                                                                   \
  } while (0)

static void store_i32(uint8_t *bytes, size_t offset, int32_t value) {
  bytes[offset] = (uint8_t)(value & 0xff);
  bytes[offset + 1] = (uint8_t)((value >> 8) & 0xff);
  bytes[offset + 2] = (uint8_t)((value >> 16) & 0xff);
  bytes[offset + 3] = (uint8_t)((value >> 24) & 0xff);
}
static void store_u16(uint8_t *bytes, size_t offset, uint16_t value) {
  bytes[offset] = (uint8_t)(value & 0xff);
  bytes[offset + 1] = (uint8_t)((value >> 8) & 0xff);
}

/* The TS duplicatePak() fixture, byte for byte. */
static void duplicate_pak(uint8_t bytes[142], const char *path) {
  memset(bytes, 0, 142);
  memcpy(bytes, "PACK", 4);
  store_i32(bytes, 4, 14);
  store_i32(bytes, 8, 128);
  bytes[12] = 41;
  bytes[13] = 42;
  for (int ordinal = 0; ordinal < 2; ordinal++) {
    size_t offset = 14 + (size_t)ordinal * 64;
    memcpy(bytes + offset, path, strlen(path));
    store_i32(bytes, offset + 56, 12 + ordinal);
    store_i32(bytes, offset + 60, 1);
  }
}

/* The TS pak(path, text) fixture from mounts.test.ts. */
static size_t single_pak(uint8_t *bytes, const char *path, const char *text) {
  size_t length = strlen(text);
  size_t total = 12 + length + 64;
  memset(bytes, 0, total);
  memcpy(bytes, "PACK", 4);
  store_i32(bytes, 4, (int32_t)(12 + length));
  store_i32(bytes, 8, 64);
  memcpy(bytes + 12, text, length);
  memcpy(bytes + 12 + length, path, strlen(path));
  store_i32(bytes, 12 + length + 56, 12);
  store_i32(bytes, 12 + length + 60, (int32_t)length);
  return total;
}

static bool write_file(const char *path, const void *data, size_t length) {
  FILE *file = fopen(path, "wb");
  if (file == NULL) return false;
  bool ok = length == 0 || fwrite(data, 1, length, file) == length;
  return fclose(file) == 0 && ok;
}
static void make_dir(const char *path) { (void)mkdir(path, 0777); }

static void test_memory_pak(void) {
  uint8_t bytes[142];
  duplicate_pak(bytes, "Models\\duplicate.mdl");
  qa_archive_handle *archive =
      qa_decode_archive(bytes, sizeof bytes, false, QA_ARCHIVE_FORMAT_PAK, NULL);
  CHECK(archive != NULL);
  bytes[12] = 0; /* caller mutation must not affect retained bytes */
  CHECK(archive->entry_count == 2);
  CHECK(archive->entries[0].ordinal == 0 && archive->entries[1].ordinal == 1);
  CHECK(strcmp(archive->entries[0].raw_path, "Models\\duplicate.mdl") == 0);
  const qa_archive_entry *found[4];
  CHECK(qa_archive_find_entries(archive, "Models/duplicate.mdl",
                                QA_ARCHIVE_PATH_EXACT, found, 4) == 2);
  CHECK(found[0]->ordinal == 0 && found[1]->ordinal == 1);
  CHECK(qa_archive_find_entries(archive, "models/duplicate.mdl",
                                QA_ARCHIVE_PATH_EXACT, found, 4) == 0);
  CHECK(qa_archive_find_entries(archive, "models/duplicate.mdl",
                                QA_ARCHIVE_PATH_ASCII_INSENSITIVE, found, 4) == 2);
  size_t length = 0;
  uint8_t *first = qa_archive_read_entry_sync_ordinal(archive, 0, &length);
  CHECK(length == 1 && first != NULL && first[0] == 41);
  free(first);
  uint8_t *second = qa_archive_read_entry_ordinal(archive, 1, &length);
  CHECK(length == 1 && second != NULL && second[0] == 42);
  free(second);
  uint8_t *via_entry =
      qa_archive_read_entry_entry(archive, &archive->entries[1], &length);
  CHECK(length == 1 && via_entry != NULL && via_entry[0] == 42);
  free(via_entry);
  CHECK_ABORTS((void)qa_archive_read_entry_sync_ordinal(archive, 2, &length));
  qa_archive_close(archive);
  CHECK_ABORTS((void)qa_archive_read_entry_sync_ordinal(archive, 0, &length));
  qa_archive_destroy(archive);

  duplicate_pak(bytes, "Models\\duplicate.mdl");
  CHECK_ABORTS((void)qa_decode_archive(bytes, 100, false,
                                       QA_ARCHIVE_FORMAT_PAK, NULL));
  uint8_t unsafe[142];
  duplicate_pak(unsafe, "Models\\duplicate.mdl");
  memset(unsafe + 14, 0, 56);
  memcpy(unsafe + 14, "../escape", 9);
  CHECK_ABORTS((void)qa_decode_archive(unsafe, sizeof unsafe, false,
                                       QA_ARCHIVE_FORMAT_PAK, NULL));
  char *normalized =
      qa_normalize_entry_path("models/monsters/tank/../ctank/skin.pcx");
  CHECK(normalized != NULL &&
        strcmp(normalized, "models/monsters/ctank/skin.pcx") == 0);
  free(normalized);
  CHECK_ABORTS(free(qa_normalize_entry_path("../escape")));
  CHECK_ABORTS(free(qa_normalize_entry_path("a//b")));
  CHECK_ABORTS(free(qa_normalize_entry_path("a/./b")));
  CHECK_ABORTS(free(qa_normalize_entry_path("c:/abs")));
}

static void test_case_rules(void) {
  uint8_t bytes[142];
  /* "Models/\xC9lite.mdl" as a byte string (É = U+00C9). */
  duplicate_pak(bytes, "Models/\xc9lite.mdl");
  qa_archive_handle *archive =
      qa_decode_archive(bytes, sizeof bytes, false, QA_ARCHIVE_FORMAT_PAK, NULL);
  CHECK(archive != NULL);
  const qa_archive_entry *found[4];
  for (int repeat = 0; repeat < 3; repeat++) {
    CHECK(qa_archive_find_entries(archive, "Models/\xc9lite.mdl",
                                  QA_ARCHIVE_PATH_EXACT, found, 4) == 2);
    CHECK(qa_archive_find_entries(archive, "models/\xe9lite.mdl",
                                  QA_ARCHIVE_PATH_ASCII_INSENSITIVE, found,
                                  4) == 0);
    CHECK(qa_archive_find_entries(archive, "models/\xe9lite.mdl",
                                  QA_ARCHIVE_PATH_CASE_INSENSITIVE, found,
                                  4) == 2);
    CHECK(qa_archive_find_entries(archive, "models\\\xc9LITE.mdl",
                                  QA_ARCHIVE_PATH_ASCII_INSENSITIVE, found,
                                  4) == 2);
    CHECK(qa_archive_find_entries(archive, "missing",
                                  QA_ARCHIVE_PATH_CASE_INSENSITIVE, found,
                                  4) == 0);
  }
  CHECK_ABORTS((void)qa_archive_find_entries(
      archive, "../escape", QA_ARCHIVE_PATH_CASE_INSENSITIVE, found, 4));
  qa_archive_destroy(archive);
}

/* Stored + deflated ZIP fixture built in-test. */
static size_t build_zip(uint8_t *out, size_t capacity) {
  (void)capacity;
  const char *names[3] = {"music/", "music/intro.ogg", "readme.txt"};
  const char *texts[3] = {"", "track-data-track-data", "hello zip world"};
  uint16_t methods[3] = {0, 8, 0};
  size_t local_offsets[3];
  size_t at = 0;
  uint8_t compressed_store[3][64];
  size_t compressed_lengths[3];
  uint32_t crcs[3];
  for (int i = 0; i < 3; i++) {
    size_t text_length = strlen(texts[i]);
    crcs[i] = (uint32_t)crc32(crc32(0L, Z_NULL, 0),
                              (const Bytef *)texts[i], (uInt)text_length);
    if (methods[i] == 0) {
      memcpy(compressed_store[i], texts[i], text_length);
      compressed_lengths[i] = text_length;
    } else {
      z_stream stream;
      memset(&stream, 0, sizeof stream);
      (void)deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                         -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
      stream.next_in = (Bytef *)(const void *)texts[i];
      stream.avail_in = (uInt)text_length;
      stream.next_out = compressed_store[i];
      stream.avail_out = sizeof compressed_store[i];
      (void)deflate(&stream, Z_FINISH);
      compressed_lengths[i] = (size_t)stream.total_out;
      (void)deflateEnd(&stream);
    }
    local_offsets[i] = at;
    store_i32(out, at + 0, 0x04034b50);
    store_u16(out, at + 4, 20);
    store_u16(out, at + 6, 0);
    store_u16(out, at + 8, methods[i]);
    store_u16(out, at + 10, 0);
    store_u16(out, at + 12, 0);
    store_i32(out, at + 14, (int32_t)crcs[i]);
    store_i32(out, at + 18, (int32_t)compressed_lengths[i]);
    store_i32(out, at + 22, (int32_t)text_length);
    store_u16(out, at + 26, (uint16_t)strlen(names[i]));
    store_u16(out, at + 28, 0);
    at += 30;
    memcpy(out + at, names[i], strlen(names[i]));
    at += strlen(names[i]);
    memcpy(out + at, compressed_store[i], compressed_lengths[i]);
    at += compressed_lengths[i];
  }
  size_t central = at;
  for (int i = 0; i < 3; i++) {
    store_i32(out, at + 0, 0x02014b50);
    store_u16(out, at + 4, 20);
    store_u16(out, at + 6, 20);
    store_u16(out, at + 8, 0);
    store_u16(out, at + 10, methods[i]);
    store_u16(out, at + 12, 0);
    store_u16(out, at + 14, 0);
    store_i32(out, at + 16, (int32_t)crcs[i]);
    store_i32(out, at + 20, (int32_t)compressed_lengths[i]);
    store_i32(out, at + 24, (int32_t)strlen(texts[i]));
    store_u16(out, at + 28, (uint16_t)strlen(names[i]));
    store_u16(out, at + 30, 0);
    store_u16(out, at + 32, 0);
    store_u16(out, at + 34, 0);
    store_u16(out, at + 36, 0);
    store_i32(out, at + 38, 0);
    store_i32(out, at + 42, (int32_t)local_offsets[i]);
    at += 46;
    memcpy(out + at, names[i], strlen(names[i]));
    at += strlen(names[i]);
  }
  size_t central_length = at - central;
  store_i32(out, at + 0, 0x06054b50);
  store_u16(out, at + 4, 0);
  store_u16(out, at + 6, 0);
  store_u16(out, at + 8, 3);
  store_u16(out, at + 10, 3);
  store_i32(out, at + 12, (int32_t)central_length);
  store_i32(out, at + 16, (int32_t)central);
  store_u16(out, at + 20, 0);
  return at + 22;
}

static void test_zip(void) {
  uint8_t bytes[1024];
  memset(bytes, 0, sizeof bytes);
  size_t length = build_zip(bytes, sizeof bytes);
  qa_archive_handle *archive = qa_decode_archive(
      bytes, length, true, QA_ARCHIVE_FORMAT_PK3, "music.pk3");
  CHECK(archive != NULL);
  CHECK(archive->format == QA_ARCHIVE_FORMAT_PK3);
  CHECK(archive->entry_count == 3);
  CHECK(archive->entries[0].is_directory);
  CHECK(strcmp(archive->entries[0].path, "music/") == 0);
  size_t out_length = 0;
  uint8_t *deflated = qa_archive_read_entry_sync_ordinal(archive, 1, &out_length);
  CHECK(out_length == strlen("track-data-track-data") && deflated != NULL &&
        memcmp(deflated, "track-data-track-data", out_length) == 0);
  free(deflated);
  uint8_t *stored = qa_archive_read_entry_sync_ordinal(archive, 2, &out_length);
  CHECK(out_length == strlen("hello zip world") && stored != NULL &&
        memcmp(stored, "hello zip world", out_length) == 0);
  free(stored);
  CHECK_ABORTS(
      (void)qa_archive_read_entry_sync_ordinal(archive, 0, &out_length));
  qa_archive_destroy(archive);
  CHECK_ABORTS((void)qa_decode_archive(bytes, length - 10, true,
                                       QA_ARCHIVE_FORMAT_PK3, "music.pk3"));
}

static char scratch[128];

static const char *scratch_path(const char *name) {
  static char buffer[256];
  (void)snprintf(buffer, sizeof buffer, "%s/%s", scratch, name);
  return buffer;
}

static void test_loose(void) {
  const char *asset = scratch_path("asset");
  char asset_copy[256];
  (void)snprintf(asset_copy, sizeof asset_copy, "%s", asset);
  CHECK(write_file(asset_copy, "first", 5));
  size_t length = 0;
  uint8_t *bytes = qa_read_loose_entry(scratch, "asset", &length);
  CHECK(length == 5 && bytes != NULL && memcmp(bytes, "first", 5) == 0);
  free(bytes);
  qa_loose_entry_handle retained;
  CHECK(qa_open_loose_entry(scratch, "asset", &retained));
  CHECK(write_file(asset_copy, "changed", 7));
  CHECK_ABORTS((void)qa_loose_entry_read(&retained, &length));
  qa_loose_entry_destroy(&retained);
  char link_path[256];
  (void)snprintf(link_path, sizeof link_path, "%s/outside", scratch);
  (void)symlink("/etc/passwd", link_path);
  qa_loose_entry_handle escape;
  memset(&escape, 0, sizeof escape);
  CHECK_ABORTS((void)qa_open_loose_entry(scratch, "outside", &escape));
  CHECK_ABORTS((void)qa_open_loose_entry(scratch, "../outside", &escape));
}

static void test_paths_and_roots(void) {
  char *normalized = qa_normalize_resource_path("maps\\test.bsp");
  CHECK(normalized != NULL && strcmp(normalized, "maps/test.bsp") == 0);
  free(normalized);
  CHECK_ABORTS(free(qa_normalize_resource_path("../outside")));
  CHECK_ABORTS(free(qa_normalize_resource_path("/abs")));
  CHECK_ABORTS(free(qa_normalize_resource_path("c:/abs")));
  char *within = qa_path_within_root("/root/base", "sub/file.txt");
  CHECK(within != NULL && strcmp(within, "/root/base/sub/file.txt") == 0);
  free(within);
  CHECK(qa_is_missing_file(ENOENT));
  CHECK(qa_is_missing_file(ENOTDIR));
  CHECK(!qa_is_missing_file(EACCES));

  /* findContentPath resolves spelling case-insensitively. */
  char sub[192];
  (void)snprintf(sub, sizeof sub, "%s/Maps", scratch);
  make_dir(sub);
  char file[256];
  (void)snprintf(file, sizeof file, "%s/Test.BSP", sub);
  CHECK(write_file(file, "x", 1));
  char *found = NULL;
  CHECK(qa_find_content_path(scratch, "maps/test.bsp",
                             QA_CONTENT_PATH_CASE_INSENSITIVE, &found));
  CHECK(found != NULL && strstr(found, "Test.BSP") != NULL);
  free(found);
  found = NULL;
  CHECK(!qa_find_content_path(scratch, "maps/test.bsp",
                              QA_CONTENT_PATH_EXACT, &found));
  CHECK(found == NULL);
  CHECK(!qa_find_content_path(scratch, "missing/file.txt",
                              QA_CONTENT_PATH_CASE_INSENSITIVE, &found));

  CHECK(strcmp(qa_game_family_directory(QA_GAME_FAMILY_Q1), "q1") == 0);
  CHECK(strcmp(qa_game_family_directory(QA_GAME_FAMILY_Q3), "q3a") == 0);
  qa_game_family family;
  CHECK(qa_game_family_for_directory("Q3A", &family) &&
        family == QA_GAME_FAMILY_Q3);
  CHECK(!qa_game_family_for_directory("quake", &family));
  char *relative = qa_game_relative_path("q1/id1/pak0.pak", QA_GAME_FAMILY_Q1);
  CHECK(relative != NULL && strcmp(relative, "id1/pak0.pak") == 0);
  free(relative);
  relative = qa_game_relative_path("other/id1/pak0.pak", QA_GAME_FAMILY_Q1);
  CHECK(relative != NULL && strcmp(relative, "other/id1/pak0.pak") == 0);
  free(relative);

  /* Game roots: canonical family dir with an anchor wins. */
  char corpus[192];
  (void)snprintf(corpus, sizeof corpus, "%s/corpus", scratch);
  make_dir(corpus);
  char q1[256], id1[320];
  (void)snprintf(q1, sizeof q1, "%s/q1", corpus);
  make_dir(q1);
  (void)snprintf(id1, sizeof id1, "%s/id1", q1);
  make_dir(id1);
  char *root = qa_game_root_in(corpus, QA_GAME_FAMILY_Q1);
  CHECK(root != NULL && strcmp(root, q1) == 0);
  free(root);
  CHECK(qa_has_game_anchor(corpus));
  CHECK(qa_game_root_in(corpus, QA_GAME_FAMILY_Q2) == NULL);
  qa_game_roots roots = qa_resolve_game_roots(corpus);
  CHECK(roots.roots[QA_GAME_FAMILY_Q1] != NULL);
  CHECK(roots.roots[QA_GAME_FAMILY_Q2] == NULL);
  qa_game_roots_free(&roots);

  char *user_root = qa_default_user_content_root();
  CHECK(user_root != NULL &&
        strstr(user_root, ".local/share/quake-typescript/content") != NULL);
  free(user_root);
  char *product = qa_user_product_directory("/user/content", "q3a/baseq3");
  CHECK(product != NULL && strcmp(product, "/user/content/q3a/baseq3") == 0);
  free(product);

  qa_content_root_search search = {corpus, scratch};
  char *default_root = qa_resolve_default_content_root(search, false);
  CHECK(default_root != NULL && strcmp(default_root, corpus) == 0);
  free(default_root);
}

static qa_content_mount archive_mount(const char *id, const char *content,
                                      const char *path, char *digest) {
  qa_content_mount mount;
  memset(&mount, 0, sizeof mount);
  mount.kind = QA_CONTENT_MOUNT_ARCHIVE;
  mount.as.archive.identity = qa_create_mount_identity(id, content, 0);
  mount.as.archive.format = QA_ARCHIVE_FORMAT_PAK;
  mount.as.archive.archive_path = path;
  mount.as.archive.archive_digest = digest;
  return mount;
}
static qa_content_mount loose_mount(const char *id, const char *content,
                                    const char *root) {
  qa_content_mount mount;
  memset(&mount, 0, sizeof mount);
  mount.kind = QA_CONTENT_MOUNT_LOOSE;
  mount.as.loose.identity = qa_create_mount_identity(id, content, 0);
  mount.as.loose.root_path = root;
  return mount;
}

static void test_mounts(void) {
  char *digest = qa_digest_bytes((const uint8_t *)"abc", 3);
  CHECK(digest != NULL &&
        strcmp(digest,
               "sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff"
               "61f20015ad") == 0);
  free(digest);

  char mroot[192];
  (void)snprintf(mroot, sizeof mroot, "%s/mounts", scratch);
  make_dir(mroot);
  char low_path[256], high_path[256], maps_dir[256];
  (void)snprintf(low_path, sizeof low_path, "%s/low.pak", mroot);
  (void)snprintf(high_path, sizeof high_path, "%s/high.pak", mroot);
  (void)snprintf(maps_dir, sizeof maps_dir, "%s/maps", mroot);
  make_dir(maps_dir);
  uint8_t pak_bytes[256];
  size_t low_length = single_pak(pak_bytes, "maps/test.bsp", "low");
  CHECK(write_file(low_path, pak_bytes, low_length));
  size_t high_length = single_pak(pak_bytes, "maps/test.bsp", "high");
  CHECK(write_file(high_path, pak_bytes, high_length));
  char loose_bsp[384];
  (void)snprintf(loose_bsp, sizeof loose_bsp, "%s/loose.bsp", maps_dir);
  CHECK(write_file(loose_bsp, "loose", 5));

  char *low_digest = qa_digest_file(low_path);
  char *high_digest = qa_digest_file(high_path);
  CHECK(low_digest != NULL && high_digest != NULL);
  CHECK(strcmp(low_digest, high_digest) != 0);
  char *again = qa_digest_file(low_path);
  CHECK(again != NULL && strcmp(again, low_digest) == 0);
  free(again);

  qa_content_mount mounts[3] = {
      archive_mount("mount:test:high", "q2:rerelease:baseq2:installed",
                    high_path, high_digest),
      archive_mount("mount:test:low", "q2:classic:baseq2:installed", low_path,
                    low_digest),
      loose_mount("mount:test:loose", "q2:classic:baseq2:installed", mroot)};
  qa_mount_id order[3] = {"mount:test:high", "mount:test:low",
                          "mount:test:loose"};
  qa_resolved_mount_plan plan;
  memset(&plan, 0, sizeof plan);
  plan.id = "mount-plan:test:1";
  plan.mounts = mounts;
  plan.mount_count = 3;
  plan.default_order = order;
  plan.default_order_count = 3;

  qa_content_mounted *mounted = qa_open_mount_plan(&plan, NULL);
  CHECK(mounted != NULL);
  qa_content_opened_resource opened;
  memset(&opened, 0, sizeof opened);
  CHECK(qa_content_mounted_open(mounted, "MAPS/test.bsp", NULL, NULL, &opened));
  CHECK(opened.byte_length == 4 && opened.bytes != NULL &&
        memcmp(opened.bytes, "high", 4) == 0);
  CHECK(opened.reference != NULL);
  CHECK(!qa_can_download_resource(opened.reference));
  free(opened.bytes);
  size_t length = 0;
  uint8_t *bytes =
      qa_content_mounted_read_reference(mounted, opened.reference, &length);
  CHECK(length == 4 && bytes != NULL && memcmp(bytes, "high", 4) == 0);
  free(bytes);
  CHECK(qa_content_mounted_exists(mounted, "maps/loose.bsp"));
  CHECK(!qa_content_mounted_exists(mounted, "maps/missing.bsp"));
  size_t list_count = 0;
  char **listed =
      qa_content_mounted_list_files(mounted, "maps", ".bsp", &list_count);
  bool saw_test = false, saw_loose = false;
  for (size_t i = 0; i < list_count; i++) {
    if (strcmp(listed[i], "test.bsp") == 0) saw_test = true;
    if (strcmp(listed[i], "loose.bsp") == 0) saw_loose = true;
  }
  CHECK(saw_test && saw_loose);
  qa_content_free_string_list(listed, list_count);

  const qa_resolved_resource_reference *loose_ref = NULL;
  CHECK(qa_content_mounted_resolve(mounted, "maps/loose.bsp", &loose_ref));
  CHECK(write_file(loose_bsp, "changed", 7));
  const qa_resolved_resource_reference stale = *loose_ref;
  CHECK_ABORTS((void)qa_content_mounted_read_reference(mounted, &stale, &length));
  CHECK_ABORTS((void)qa_content_mounted_read_path(mounted, "../outside", &length));

  qa_open_mount_options pure;
  memset(&pure, 0, sizeof pure);
  pure.has_pure = true;
  qa_content_digest pure_digests[2] = {low_digest, high_digest};
  pure.pure.archives = pure_digests;
  pure.pure.archive_count = 2;
  qa_content_mounted *pure_mounted = qa_open_mount_plan(&plan, &pure);
  CHECK(pure_mounted != NULL);
  bytes = qa_content_mounted_read_path(pure_mounted, "maps/test.bsp", &length);
  CHECK(length == 3 && bytes != NULL && memcmp(bytes, "low", 3) == 0);
  free(bytes);
  memset(&opened, 0, sizeof opened);
  CHECK(!qa_content_mounted_open(pure_mounted, "maps/loose.bsp", NULL, NULL,
                                 &opened));
  qa_content_mounted_destroy(pure_mounted);

  /* borrowMountPlan over identical sources; close keeps parent usable. */
  qa_content_mounted *borrowed =
      qa_content_mounted_borrow_mount_plan(mounted, &plan, NULL);
  CHECK(borrowed != NULL);
  bytes = qa_content_mounted_read_path(borrowed, "maps/test.bsp", &length);
  CHECK(length == 4 && bytes != NULL && memcmp(bytes, "high", 4) == 0);
  free(bytes);
  qa_content_mounted_destroy(borrowed);
  bytes = qa_content_mounted_read_path(mounted, "maps/test.bsp", &length);
  CHECK(length == 4 && bytes != NULL);
  free(bytes);

  qa_content_mounted_destroy(mounted);

  /* Repeated order is a RangeError. */
  qa_mount_id bad_order[3] = {"mount:test:high", "mount:test:high",
                              "mount:test:loose"};
  qa_resolved_mount_plan bad = plan;
  bad.default_order = bad_order;
  CHECK_ABORTS((void)qa_open_mount_plan(&bad, NULL));
  free(low_digest);
  free(high_digest);
}

static const char *const GRIP_JSON =
    "{\"origin\":{\"x\":1,\"y\":2,\"z\":3},\"axis\":["
    "{\"x\":1,\"y\":0,\"z\":0},{\"x\":0,\"y\":1,\"z\":0},"
    "{\"x\":0,\"y\":0,\"z\":1}]}";
static const char *const DIGEST_JSON =
    "\"sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\"";

static qa_save_value *parse(const char *json) {
  return qa_parse_json_value((const uint8_t *)json, strlen(json), false);
}

static void test_declarations(void) {
  char json[1024];
  (void)snprintf(json, sizeof json,
                 "{\"version\":1,\"digest\":%s,\"kind\":\"mesh\","
                 "\"referenceFrame\":2,\"vertices\":[3,1,4],\"grip\":%s}",
                 DIGEST_JSON, GRIP_JSON);
  qa_model_attachment_definition attachment;
  CHECK(qa_content_read_model_attachment((const uint8_t *)json, strlen(json),
                                         &attachment));
  CHECK(!attachment.is_joint);
  CHECK(attachment.reference_frame == 2);
  CHECK(attachment.vertices[0] == 3 && attachment.vertices[1] == 1 &&
        attachment.vertices[2] == 4);
  CHECK(attachment.grip.origin.x == 1 && attachment.grip.origin.z == 3);
  CHECK(attachment.grip.scale.x == 1 && attachment.grip.scale.y == 1);
  qa_content_model_attachment_free(&attachment);

  (void)snprintf(json, sizeof json,
                 "{\"version\":1,\"digest\":%s,\"kind\":\"mesh\","
                 "\"referenceFrame\":0,\"vertices\":[1,1,2],\"grip\":%s}",
                 DIGEST_JSON, GRIP_JSON);
  qa_model_attachment_definition aborted;
  CHECK_ABORTS((void)qa_content_read_model_attachment(
      (const uint8_t *)json, strlen(json), &aborted));

  qa_held_weapon_declaration held;
  (void)snprintf(json, sizeof json, "{\"version\":1,\"kind\":\"none\"}");
  CHECK(qa_read_held_weapon_file((const uint8_t *)json, strlen(json), &held));
  CHECK(!held.is_model);
  qa_content_held_weapon_free(&held);
  (void)snprintf(
      json, sizeof json,
      "{\"version\":1,\"kind\":\"model\",\"model\":{\"path\":\"models/v.mdl\","
      "\"referenceFrame\":0,\"grip\":%s,\"digest\":%s,"
      "\"part\":{\"digests\":[%s],\"vertices\":[5,9]}}}",
      GRIP_JSON, DIGEST_JSON, DIGEST_JSON);
  CHECK(qa_read_held_weapon_file((const uint8_t *)json, strlen(json), &held));
  CHECK(held.is_model);
  CHECK(strcmp(held.model.path, "models/v.mdl") == 0);
  CHECK(held.model.has_digest);
  CHECK(held.model.part != NULL && held.model.part->digests.count == 1 &&
        held.model.part->vertices.count == 2 &&
        held.model.part->vertices.values[1] == 9);
  qa_content_held_weapon_free(&held);

  qa_save_value *icon_json =
      parse("{\"kind\":\"wad-picture\",\"path\":\"gfx/wad.wad\","
            "\"lump\":\"shotgun\"}");
  qa_save_reader reader;
  CHECK(qa_save_reader_init(&reader, icon_json, NULL));
  qa_source_item_icon_declaration icon;
  CHECK(qa_read_item_icon_declaration(&reader, &icon));
  qa_save_reader_destroy(&reader);
  qa_weapon_hud_icon hud =
      qa_resolve_item_icon(icon, "q1:classic:id1:installed");
  CHECK(hud.kind == QA_WEAPON_HUD_ICON_WAD_PICTURE);
  CHECK(strcmp(hud.resource.path, "gfx/wad.wad") == 0);
  CHECK(strcmp(hud.lump, "shotgun") == 0);
  qa_content_item_icon_free(&icon);
  qa_save_value_free(icon_json);

  icon_json = parse("{\"kind\":\"shader\",\"resource\":"
                    "{\"content\":\"q3:classic:baseq3:installed\"},"
                    "\"content\":\"q3:classic:baseq3:installed\","
                    "\"name\":\"icons/railgun\"}");
  /* readSourceItemIcon reads content from the reader itself for shaders. */
  CHECK(qa_save_reader_init(&reader, icon_json, NULL));
  qa_weapon_hud_icon shader_icon;
  CHECK(qa_read_source_item_icon(&reader, "q3:classic:baseq3:installed",
                                 &shader_icon));
  qa_save_reader_destroy(&reader);
  CHECK(shader_icon.kind == QA_WEAPON_HUD_ICON_SHADER);
  CHECK(strcmp(shader_icon.name, "icons/railgun") == 0);
  qa_content_weapon_hud_icon_free(&shader_icon);
  qa_save_value_free(icon_json);
}

int main(void) {
  char home_template[] = "/tmp/qa-content-home-XXXXXX";
  char *home = mkdtemp(home_template);
  CHECK(home != NULL);
  (void)setenv("HOME", home, 1);
  char scratch_template[] = "/tmp/qa-content-archive-XXXXXX";
  char *made = mkdtemp(scratch_template);
  CHECK(made != NULL);
  (void)snprintf(scratch, sizeof scratch, "%s", made);

  test_memory_pak();
  test_case_rules();
  test_zip();
  test_loose();
  test_paths_and_roots();
  test_mounts();
  test_declarations();

  printf("test_content_archive: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
