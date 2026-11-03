/*
 * Port of key cases from the tests/content/catalog TS tests
 * (addons, start-maps, catalog, weapons, equipment, monsters,
 * native/qvm weapon behavior documents). TypeScript throw cases
 * run in a child process and must abort. Archive inspection is a
 * fake service (the TS openArchive port is pending; see
 * include/qa/content/catalog/index.h). GPL-2.0-or-later.
 */
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

#include "qa/app/bootstrap/content.h"
#include "qa/content/catalog.h"

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

static qa_save_value *json(const char *text) {
  return qa_parse_json_value((const uint8_t *)text, strlen(text), false);
}

static void test_products(void) {
  CHECK(qa_expected_product_count == 40);
  CHECK(strcmp(qa_expected_products[0].id, "q1-classic-id1") == 0);
  CHECK(qa_expected_products[0].family == QA_GAME_FAMILY_Q1);
  CHECK(strcmp(qa_expected_products[0].content_directory, "q1/id1") == 0);
  CHECK(qa_expected_products[0].base_product == NULL);
  CHECK(qa_expected_products[0].required_content_archive_count == 2);
  CHECK(strcmp(qa_expected_products[39].id, "q3-demota") == 0);
  CHECK(strcmp(qa_expected_products[39].edition, "demo") == 0);
}

static void test_remote(void) {
  qa_remote_content_selection selection = {0};
  CHECK(qa_remote_content_selection_make("q1-quakeworld", "ID1", &selection));
  CHECK(strcmp(selection.directory, "qw") == 0);
  qa_remote_content_selection_free(&selection);
  CHECK(qa_remote_content_selection_make("q2-classic-baseq2", "", &selection));
  CHECK(strcmp(selection.directory, "baseq2") == 0);
  char *product = qa_remote_content_product(&selection, NULL, 0);
  CHECK(product != NULL && strcmp(product, "q2-classic-baseq2") == 0);
  free(product);
  qa_remote_content_selection_free(&selection);
  CHECK(qa_remote_content_selection_make("q3-baseq3", "MyMod", &selection));
  CHECK(strcmp(selection.directory, "mymod") == 0);
  product = qa_remote_content_product(&selection, NULL, 0);
  CHECK(product != NULL && strcmp(product, "q3-baseq3-mod-mymod") == 0);
  free(product);
  qa_remote_content_selection_free(&selection);
  CHECK_ABORTS(qa_remote_content_selection_make("q3-baseq3", "../evil", &selection));
  CHECK_ABORTS(qa_remote_content_selection_make("q3-baseq3", "a/b", &selection));
}

static qa_catalog_archive make_archive(const char *path, qa_archive_format format) {
  qa_catalog_archive archive = {0};
  archive.path = strdup(path);
  archive.format = format;
  return archive;
}

static void test_order_archives(void) {
  qa_catalog_archive storage[4];
  storage[0] = make_archive("/x/pak1.pak", QA_ARCHIVE_FORMAT_PAK);
  storage[1] = make_archive("/x/pak0.pak", QA_ARCHIVE_FORMAT_PAK);
  storage[2] = make_archive("/x/zeta.pk3", QA_ARCHIVE_FORMAT_PK3);
  storage[3] = make_archive("/x/ignored.zip", QA_ARCHIVE_FORMAT_ZIP);
  qa_catalog_archive *archives[4] = {&storage[0], &storage[1], &storage[2],
                                     &storage[3]};
  size_t count = 0;
  qa_catalog_archive **ordered =
      qa_order_game_archives(&qa_expected_products[0], archives, 4, &count);
  CHECK(count == 3);
  /* Highest priority first: pk3, pak1, pak0. */
  CHECK(strcmp(ordered[0]->path, "/x/zeta.pk3") == 0);
  CHECK(strcmp(ordered[1]->path, "/x/pak1.pak") == 0);
  CHECK(strcmp(ordered[2]->path, "/x/pak0.pak") == 0);
  free(ordered);
  /* Q2 numeric pak order, reversed. */
  storage[0] = make_archive("/x/pak10.pak", QA_ARCHIVE_FORMAT_PAK);
  storage[1] = make_archive("/x/pak2.pak", QA_ARCHIVE_FORMAT_PAK);
  storage[2] = make_archive("/x/pak0.pkz", QA_ARCHIVE_FORMAT_ZIP);
  const qa_catalog_product_expectation *q2 = NULL;
  for (size_t i = 0; i < qa_expected_product_count; i++)
    if (strcmp(qa_expected_products[i].id, "q2-classic-baseq2") == 0)
      q2 = &qa_expected_products[i];
  qa_catalog_archive *q2_archives[3] = {&storage[0], &storage[1], &storage[2]};
  ordered = qa_order_game_archives(q2, q2_archives, 3, &count);
  CHECK(count == 3);
  CHECK(strcmp(ordered[0]->path, "/x/pak10.pak") == 0);
  CHECK(strcmp(ordered[2]->path, "/x/pak0.pkz") == 0);
  free(ordered);
  for (size_t i = 0; i < 4; i++) qa_catalog_archive_free(&storage[i]);

  /* Q3: FS_PathCmp keys (upper-case ASCII, `\\` and `:` as `/`), at most
   * 1024 pk3s (bun: b, a_x, aZ, a:b; 1030 archives keep z1023..z0000). */
  const qa_catalog_product_expectation *q3 = NULL;
  for (size_t i = 0; i < qa_expected_product_count && q3 == NULL; i++)
    if (qa_expected_products[i].family == QA_GAME_FAMILY_Q3) q3 = &qa_expected_products[i];
  CHECK(q3 != NULL && strcmp(q3->id, "q3-baseq3") == 0);
  storage[0] = make_archive("/x/a_x.pk3", QA_ARCHIVE_FORMAT_PK3);
  storage[1] = make_archive("/x/aZ.pk3", QA_ARCHIVE_FORMAT_PK3);
  storage[2] = make_archive("/x/b.pk3", QA_ARCHIVE_FORMAT_PK3);
  storage[3] = make_archive("/x/a:b.pk3", QA_ARCHIVE_FORMAT_PK3);
  ordered = qa_order_game_archives(q3, archives, 4, &count);
  CHECK(count == 4 && strcmp(ordered[0]->path, "/x/b.pk3") == 0 &&
        strcmp(ordered[1]->path, "/x/a_x.pk3") == 0 && strcmp(ordered[2]->path, "/x/aZ.pk3") == 0 &&
        strcmp(ordered[3]->path, "/x/a:b.pk3") == 0);
  free(ordered);
  for (size_t i = 0; i < 4; i++) qa_catalog_archive_free(&storage[i]);
  static qa_catalog_archive many[1030];
  static qa_catalog_archive *many_pointers[1030];
  for (size_t i = 0; i < 1030; i++) {
    char name[32];
    snprintf(name, sizeof name, "/x/z%04zu.pk3", i);
    many[i] = make_archive(name, QA_ARCHIVE_FORMAT_PK3);
    many_pointers[i] = &many[i];
  }
  ordered = qa_order_game_archives(q3, many_pointers, 1030, &count);
  CHECK(count == 1024 && strcmp(ordered[0]->path, "/x/z1023.pk3") == 0 &&
        strcmp(ordered[1023]->path, "/x/z0000.pk3") == 0);
  free(ordered);
  for (size_t i = 0; i < 1030; i++) qa_catalog_archive_free(&many[i]);
}

/* required-programs.test.ts: requiredPrograms are enforced through the base
 * chain during availability. */
static void write_pak_with(const char *path, const char *const *entries, size_t count) {
  size_t directory_offset = 12 + count;
  size_t length = directory_offset + count * 64;
  uint8_t *bytes = calloc(1, length);
  memcpy(bytes, "PACK", 4);
  bytes[4] = (uint8_t)directory_offset;
  bytes[8] = (uint8_t)(count * 64);
  for (size_t i = 0; i < count; i++) {
    bytes[12 + i] = 6;
    size_t offset = directory_offset + i * 64;
    memcpy(bytes + offset, entries[i], strlen(entries[i]));
    bytes[offset + 56] = (uint8_t)(12 + i);
    bytes[offset + 60] = 1;
  }
  FILE *file = fopen(path, "wb");
  if (file != NULL) {
    (void)fwrite(bytes, 1, length, file);
    (void)fclose(file);
  }
  free(bytes);
}

static void test_required_programs(void) {
  char root_template[] = "/tmp/qa-required-programs-XXXXXX";
  char *root = mkdtemp(root_template);
  CHECK(root != NULL);
  if (root == NULL) return;
  char path[1024];
  const char *const directories[3] = {"id1", "qw", "hipnotic"};
  for (size_t i = 0; i < 3; i++) {
    snprintf(path, sizeof path, "%s/%s", root, directories[i]);
    (void)mkdir(path, 0777);
  }
  const char *const progs[1] = {"progs.dat"};
  const char *const maps[1] = {"maps/e1m1.bsp"};
  snprintf(path, sizeof path, "%s/id1/pak0.pak", root);
  write_pak_with(path, progs, 1);
  snprintf(path, sizeof path, "%s/id1/pak1.pak", root);
  write_pak_with(path, maps, 1);
  snprintf(path, sizeof path, "%s/hipnotic/pak0.pak", root);
  write_pak_with(path, progs, 1);
  qa_catalog_product_expectation products[3];
  size_t product_count = 0;
  for (size_t i = 0; i < qa_expected_product_count; i++) {
    const char *id = qa_expected_products[i].id;
    if (strcmp(id, "q1-classic-id1") == 0 || strcmp(id, "q1-quakeworld") == 0 ||
        strcmp(id, "q1-quakeworld-hipnotic") == 0)
      products[product_count++] = qa_expected_products[i];
  }
  CHECK(product_count == 3);
  for (int pass = 0; pass < 2; pass++) {
    qa_discover_content_options options = {0};
    options.corpus_root = root;
    options.products = products;
    options.product_count = product_count;
    options.discover_mods = true;
    /* TS reads the archives through openArchive (the app's inspector). */
    options.inspector = qa_app_default_archive_inspector();
    qa_content_catalog catalog = {0};
    CHECK(qa_discover_installed_content(&options, &catalog));
    const qa_catalog_discovered_product *id1 = qa_content_catalog_product(&catalog, "q1-classic-id1");
    const qa_catalog_discovered_product *qw = qa_content_catalog_product(&catalog, "q1-quakeworld");
    const qa_catalog_discovered_product *hip =
        qa_content_catalog_product(&catalog, "q1-quakeworld-hipnotic");
    CHECK(id1 != NULL && id1->availability.kind == QA_CATALOG_INSTALLED);
    if (pass == 0) {
      CHECK(qw != NULL && qw->availability.kind == QA_CATALOG_MISSING &&
            qw->availability.requirement_count == 1 &&
            strcmp(qw->availability.requirements[0], "qwprogs.dat") == 0);
      CHECK(hip != NULL && hip->availability.kind == QA_CATALOG_MISSING);
      snprintf(path, sizeof path, "%s/qw/qwprogs.dat", root);
      FILE *file = fopen(path, "wb");
      if (file != NULL) {
        (void)fputc(6, file);
        (void)fclose(file);
      }
    } else {
      CHECK(qw != NULL && qw->availability.kind == QA_CATALOG_INSTALLED);
      CHECK(hip != NULL && hip->availability.kind == QA_CATALOG_INSTALLED);
    }
    qa_content_catalog_destroy(&catalog);
  }
  const char *const files[5] = {"id1/pak0.pak", "id1/pak1.pak", "hipnotic/pak0.pak", "qw/qwprogs.dat", NULL};
  for (size_t i = 0; files[i] != NULL; i++) {
    snprintf(path, sizeof path, "%s/%s", root, files[i]);
    (void)remove(path);
  }
  for (size_t i = 0; i < 3; i++) {
    snprintf(path, sizeof path, "%s/%s", root, directories[i]);
    (void)rmdir(path);
  }
  (void)rmdir(root);
}

static void test_start_maps(void) {
  const char *document =
      "{\"episodes\":[{\"id\":\"mod\",\"command\":\"newgame_mod\"}],\"maps\":["
      "{\"episode\":\"other\",\"sp\":true,\"bsp\":\"other\"},"
      "{\"episode\":\"mod\",\"sp\":true,\"bsp\":\"intro.cin+*zfirst\","
      "\"title\":\"First\",\"start_items\":\"weapon_shotgun;ammo_shells 20\"},"
      "{\"episode\":\"mod\",\"sp\":true,\"bsp\":\"asecond\"},"
      "{\"episode\":\"mod\",\"dm\":true,\"bsp\":\"arena\"}]}";
  qa_authored_starts starts = {0};
  CHECK(qa_parse_authored_starts((const uint8_t *)document, strlen(document),
                                 "mod", &starts));
  CHECK(starts.episode != NULL &&
        strcmp(starts.episode->command, "newgame_mod") == 0);
  CHECK(starts.start_count == 2);
  CHECK(strcmp(starts.starts[0].path, "maps/zfirst.bsp") == 0);
  CHECK(strcmp(starts.starts[1].path, "maps/asecond.bsp") == 0);
  CHECK(strcmp(starts.starts[0].bsp, "intro.cin+*zfirst") == 0);
  CHECK(strcmp(starts.starts[0].start_items,
               "weapon_shotgun;ammo_shells 20") == 0);
  CHECK(strcmp(starts.starts[0].title, "First") == 0);
  qa_authored_starts_free(&starts);
  const char *bad_flag =
      "{\"maps\":[{\"episode\":\"mod\",\"sp\":\"true\",\"bsp\":\"first\"}]}";
  qa_authored_starts bad = {0};
  CHECK_ABORTS(qa_parse_authored_starts((const uint8_t *)bad_flag,
                                        strlen(bad_flag), "mod", &bad));
}

typedef struct escape_job {
  const qa_addon_package *package;
} escape_job;
static void escape_run(void *raw) {
  escape_job *job = raw;
  bool found = false;
  free(qa_addon_install_path(job->package, "../escape", &found));
}
typedef struct skip_log {
  char *reasons[8];
  int count;
} skip_log;
static void skip_record(void *ctx, const char *reason) {
  skip_log *log = ctx;
  if (log->count < 8) log->reasons[log->count++] = strdup(reason);
}

static void test_addons(void) {
  char sha_a[65], sha_b[65];
  memset(sha_a, 'a', 64); sha_a[64] = '\0';
  memset(sha_b, 'b', 64); sha_b[64] = '\0';
  char *text = NULL;
  size_t ignored = 0;
  (void)ignored;
  /* Row mirrors addons.test.ts `row()`. */
  char buffer[4096];
  (void)snprintf(buffer, sizeof buffer,
      "[{\"sha256\":\"%s\",\"bytes\":100,\"tags\":[\"game=quake\","
      "\"game_mode=singleplayer\",\"filename=map.zip\",\"startmap=test\"],"
      "\"install\":{\"extractmapping\":{\"/\":\"/id1/\",\"/docs/\":\"/id1/maps/\","
      "\"/banner.png\":\"/id1/maps/map_banner.png\",\"/progs.dat\":null}},"
      "\"urls\":[\"https://www.quaddicted.com/files/by-sha256/aa/%s/test.zip\"]}]",
      sha_a, sha_a);
  qa_save_value *value = json(buffer);
  CHECK(value != NULL);
  qa_addon_package_list list = {0};
  CHECK(qa_parse_addon_catalog(value, &list));
  CHECK(list.count == 1);
  bool found = false;
  char *path = qa_addon_install_path(&list.values[0], "maps/test.bsp", &found);
  CHECK(found && path != NULL && strcmp(path, "id1/maps/test.bsp") == 0);
  free(path);
  path = qa_addon_install_path(&list.values[0], "docs/readme.txt", &found);
  CHECK(found && strcmp(path, "id1/maps/readme.txt") == 0);
  free(path);
  path = qa_addon_install_path(&list.values[0], "banner.png", &found);
  CHECK(found && strcmp(path, "id1/maps/map_banner.png") == 0);
  free(path);
  path = qa_addon_install_path(&list.values[0], "progs.dat", &found);
  CHECK(!found && path == NULL);
  qa_addon_package package = list.values[0];
  CHECK_ABORTS(qa_addon_install_path(&package, "../escape", &found));
  {
    escape_job job = {&package};
    qa_caught_error caught;
    CHECK(!qa_try(escape_run, &job, &caught));
    CHECK(strcmp(caught.message,
                 "File path must be relative and stay inside its storage directory") == 0);
  }
  qa_addon_package_list_free(&list);
  qa_save_value_free(value);
  /* addons.test.ts "a malformed row or a dot-directory game option skips that
   * row only" (bun: ["good.zip"]; reasons "Invalid Quaddicted package",
   * "Invalid game directory: ..", "\"not a url\" cannot be parsed as a URL."). */
  {
    char sha_c[65];
    memset(sha_c, 'c', 64);
    sha_c[64] = '\0';
    static char rows[4096];
    (void)snprintf(rows, sizeof rows,
        "[\"not a record\","
        "{\"sha256\":\"%s\",\"bytes\":100,\"tags\":[\"game=quake\",\"game_mode=singleplayer\","
        "\"filename=bad.zip\",\"commandline=-game ..\"],\"install\":{\"extract\":\"/\"},"
        "\"urls\":[\"https://www.quaddicted.com/files/by-sha256/aa/%s/test.zip\"]},"
        "{\"sha256\":\"%s\",\"bytes\":100,\"tags\":[\"game=quake\",\"game_mode=singleplayer\","
        "\"filename=badurl.zip\"],\"install\":{\"extract\":\"/\"},\"urls\":[\"not a url\"]},"
        "{\"sha256\":\"%s\",\"bytes\":100,\"tags\":[\"game=quake\",\"game_mode=singleplayer\","
        "\"filename=good.zip\"],\"install\":{\"extract\":\"/\"},"
        "\"urls\":[\"https://www.quaddicted.com/files/by-sha256/bb/%s/test.zip\"]}]",
        sha_a, sha_a, sha_c, sha_b, sha_b);
    qa_save_value *rows_value = json(rows);
    skip_log log = {0};
    qa_addon_package_list kept = {0};
    CHECK(rows_value != NULL && qa_parse_addon_catalog_reporting(rows_value, skip_record, &log, &kept));
    CHECK(kept.count == 1 && strcmp(kept.values[0].filename, "good.zip") == 0);
    CHECK(log.count == 3);
    CHECK(log.count == 3 && strcmp(log.reasons[0], "Invalid Quaddicted package") == 0 &&
          strcmp(log.reasons[1], "Invalid game directory: ..") == 0 &&
          strcmp(log.reasons[2], "\"not a url\" cannot be parsed as a URL.") == 0);
    for (int i = 0; i < log.count; i++) free(log.reasons[i]);
    qa_addon_package_list_free(&kept);
    qa_save_value_free(rows_value);
  }
  /* Dependencies ordered before overlays. */
  (void)snprintf(buffer, sizeof buffer,
      "[{\"sha256\":\"%s\",\"bytes\":100,\"tags\":[\"game=quake\","
      "\"game_mode=singleplayer\",\"filename=addon.zip\","
      "\"depends=('copper>=1.2')\",\"commandline=-game copper\"],"
      "\"install\":{\"extract\":\"/copper/maps/\"},"
      "\"urls\":[\"https://www.quaddicted.com/files/by-sha256/aa/%s/test.zip\"]},"
      "{\"sha256\":\"%s\",\"bytes\":100,\"tags\":[\"game=quake\","
      "\"game_mode=singleplayer\",\"filename=copper.zip\",\"provides='copper=1.3'\"],"
      "\"install\":{\"extract\":\"/copper/\"},"
      "\"urls\":[\"https://www.quaddicted.com/files/by-sha256/bb/%s/test.zip\"]}]",
      sha_a, sha_a, sha_b, sha_b);
  value = json(buffer);
  CHECK(qa_parse_addon_catalog(value, &list));
  CHECK(list.count == 2);
  CHECK(strcmp(list.values[0].game_directory, "copper") == 0);
  size_t resolved_count = 0;
  const qa_addon_package **resolved =
      qa_resolve_addon_packages(list.values, list.count, &list.values[0],
                                &resolved_count);
  CHECK(resolved_count == 2);
  CHECK(strcmp(resolved[0]->filename, "copper.zip") == 0);
  CHECK(strcmp(resolved[1]->filename, "addon.zip") == 0);
  free((void *)resolved);
  const qa_addon_package *selected = &list.values[0];
  CHECK_ABORTS(qa_resolve_addon_packages(selected, 1, selected, &resolved_count));
  qa_addon_package_list_free(&list);
  qa_save_value_free(value);
  (void)text;
}

static void test_nightdive(void) {
  qa_save_value *value = json(
      "{\"addons\":[{\"gamedir\":\"mg3\",\"id\":\"\",\"name\":\"Dawn\"},"
      "{\"gamedir\":\"bad/name\"},{\"gamedir\":\"plain\"}]}");
  qa_nightdive_addon_list list = {0};
  CHECK(qa_parse_nightdive_addons(value, &list));
  CHECK(list.count == 2);
  CHECK(strcmp(list.values[0].id, "mg3") == 0);
  CHECK(strcmp(list.values[0].title, "Dawn") == 0);
  CHECK(strcmp(list.values[1].title, "plain") == 0);
  qa_nightdive_addon_list_free(&list);
  qa_save_value_free(value);
  CHECK(!qa_managed_addon_hidden("/nonexistent", "q1/id1"));
}

static void test_documents(void) {
  const char *document = "{\"version\":1,\"behaviors\":[{\"id\":\"a:b\"}]}";
  qa_weapon_behavior_document parsed = {0};
  CHECK(qa_read_weapon_behavior_document((const uint8_t *)document,
                                         strlen(document), &parsed));
  CHECK(parsed.behavior_count == 1);
  CHECK(!parsed.has_artifact_path);
  CHECK(strcmp(qa_weapon_behavior_entry_id(parsed.behaviors[0]), "a:b") == 0);
  qa_weapon_behavior_document_free(&parsed);
  qa_save_value *bad = json("{\"nope\":1}");
  CHECK_ABORTS(qa_weapon_behavior_entry_id(bad));
  qa_save_value_free(bad);
  const char *native = "{\"version\":1,\"profiles\":[{\"x\":1}]}";
  qa_native_weapon_behavior_document native_doc = {0};
  CHECK(qa_read_native_weapon_behavior_document(
      (const uint8_t *)native, strlen(native), &native_doc));
  CHECK(native_doc.profile_count == 1);
  qa_native_weapon_behavior_document_free(&native_doc);
  qa_qvm_weapon_behavior_document qvm_doc = {0};
  CHECK(qa_read_qvm_weapon_behavior_document((const uint8_t *)native,
                                             strlen(native), &qvm_doc));
  CHECK(qvm_doc.profile_count == 1);
  qa_qvm_weapon_behavior_document_free(&qvm_doc);
}

static void test_timing_equipment(void) {
  qa_provider_reference reference = {"q2:test", "q2:classic:baseq2:installed"};
  qa_provider_timing timing = {0};
  CHECK(qa_native_provider_timing(reference, QA_GAME_FAMILY_Q2, true, &timing));
  CHECK(strcmp(timing.numeric.id, "q2:binary32") == 0);
  CHECK(timing.clock.kind == QA_CLOCK_Q2_RERELEASE);
  CHECK(timing.clock.frame_milliseconds == 25);
  qa_native_provider_timing_free(&timing);
  qa_equipment_selection disabled = qa_disabled_equipment();
  CHECK(!disabled.grapple.is_enabled && !disabled.hand_grenades.is_enabled);
  qa_equipment_selection equipment = {0};
  equipment.grapple.is_enabled = true;
  equipment.grapple.mechanic = "q1-threewave";
  equipment.grapple.edition = "classic";
  equipment.grapple.binding = "slot";
  equipment.grapple.source.provider = qa_equipment_provider_ids.threewave;
  equipment.grapple.source.content = "q1:classic:ctf:installed";
  qa_catalog_resource_list resources = {0};
  CHECK(qa_equipment_resources(equipment, &resources));
  CHECK(resources.count == 9);
  CHECK(strcmp(resources.values[0].path, "progs/star.mdl") == 0);
  qa_catalog_resource_list_free(&resources);
  size_t timing_count = 0;
  qa_provider_timing *timings = qa_equipment_timing(equipment, &timing_count);
  CHECK(timing_count == 1);
  CHECK(timings != NULL && timings[0].clock.kind == QA_CLOCK_Q1_NETQUAKE);
  free(timings);
}

static bool fake_inspect(void *ctx, const char *path, qa_archive_format format,
                         qa_catalog_archive *out, char **error_out) {
  (void)ctx; (void)error_out;
  memset(out, 0, sizeof *out);
  out->path = strdup(path);
  out->format = format;
  out->entries = (qa_catalog_archive_entry *)calloc(2, sizeof *out->entries);
  if (out->path == NULL || out->entries == NULL) return false;
  out->entries[0].path = strdup("maps/start.bsp");
  out->entries[0].ordinal = 0;
  /* Discovery now requires each product's programs (TS missingPrograms). */
  out->entries[1].path = strdup("progs.dat");
  out->entries[1].ordinal = 1;
  out->entry_count = 2;
  return true;
}

static void write_file_at(const char *path) {
  FILE *file = fopen(path, "wb");
  if (file != NULL) { (void)fputs("x", file); (void)fclose(file); }
}

static void test_discovery_and_weapons(void) {
  char root_template[] = "/tmp/qa-catalog-XXXXXX";
  char *root = mkdtemp(root_template);
  CHECK(root != NULL);
  if (root == NULL) return;
  char dir[1024], file[1024];
  (void)snprintf(dir, sizeof dir, "%s/q1", root);
  (void)mkdir(dir, 0777);
  (void)snprintf(dir, sizeof dir, "%s/q1/id1", root);
  (void)mkdir(dir, 0777);
  (void)snprintf(file, sizeof file, "%s/q1/id1/pak0.pak", root);
  write_file_at(file);
  (void)snprintf(file, sizeof file, "%s/q1/id1/pak1.pak", root);
  write_file_at(file);
  const qa_catalog_product_expectation *stock = &qa_expected_products[0];
  qa_discover_content_options options = {0};
  options.corpus_root = root;
  options.products = stock;
  options.product_count = 1;
  options.discover_mods = false;
  options.inspector.inspect = fake_inspect;
  qa_content_catalog catalog = {0};
  CHECK(qa_discover_installed_content(&options, &catalog));
  CHECK(catalog.product_count == 1);
  const qa_catalog_discovered_product *product =
      qa_content_catalog_product(&catalog, "q1-classic-id1");
  CHECK(product->availability.kind == QA_CATALOG_INSTALLED);
  CHECK(product->archive_count == 2);
  CHECK(product->map_count >= 1);
  size_t map_count = 0;
  const qa_catalog_content_map **maps =
      qa_content_catalog_maps_for(&catalog, product->id, &map_count);
  CHECK(map_count == 1);
  CHECK(maps != NULL && strcmp(maps[0]->path, "maps/start.bsp") == 0);
  free((void *)maps);
  CHECK_ABORTS(qa_content_catalog_product(&catalog, "nope"));
  /* Weapons on the discovered catalog. */
  CHECK(qa_supports_selected_weapon_product(&product->expectation));
  qa_provider_reference map = {"q1:official", product->id};
  qa_provider_reference weapon = {"q1:official", product->id};
  char *owned = NULL;
  qa_provider_reference canonical =
      qa_canonical_weapon_source(map, weapon, &catalog, &owned);
  CHECK(strcmp(canonical.provider, "q1:official") == 0);
  free(owned);
  qa_catalog_resource_list resources = {0};
  qa_provider_reference weapons[1] = {weapon};
  CHECK(qa_weapon_resources(map, weapons, 1, &catalog, &resources));
  CHECK(resources.count > 5);
  qa_catalog_resource_list_free(&resources);
  /* HUD: classic Q1 shotgun uses WAD pictures. */
  qa_weapon_hud_icons icons = {0};
  bool found = false;
  CHECK(qa_weapon_hud_icons_for(weapon, &product->expectation,
                                "q1:weapon/shotgun", &icons, &found));
  CHECK(found && icons.has_weapon);
  CHECK(icons.weapon.kind == QA_WEAPON_HUD_ICON_WAD_PICTURE);
  CHECK(strcmp(icons.weapon.lump, "inv_shotgun") == 0);
  CHECK(icons.has_ammo && strcmp(icons.ammo.lump, "sb_shells") == 0);
  qa_weapon_hud_icons_free(&icons);
  /* Launch selection (pure cases). */
  qa_launch_choice preset_choice = qa_preset_choice("recipe:test:1");
  CHECK(!preset_choice.map.is_selected);
  qa_validate_equipment((qa_equipment_selection){0}, &catalog);
  qa_content_catalog_destroy(&catalog);
  (void)snprintf(file, sizeof file, "%s/q1/id1/pak0.pak", root);
  (void)remove(file);
  (void)snprintf(file, sizeof file, "%s/q1/id1/pak1.pak", root);
  (void)remove(file);
  (void)rmdir(dir);
  (void)snprintf(dir, sizeof dir, "%s/q1", root);
  (void)rmdir(dir);
  (void)rmdir(root);
}

/* Rebound catalog weapon data: Q2/Q3 HUD icons resolve through the
 * landed owner tables (q2 foundation/missionpacks definitions and
 * item icons; q3 arsenal + shared itemList), not private copies. */
static const qa_catalog_product_expectation *expectation_by_id(
    const char *id) {
  for (size_t i = 0; i < qa_expected_product_count; i++)
    if (strcmp(qa_expected_products[i].id, id) == 0)
      return &qa_expected_products[i];
  return NULL;
}

static void test_weapon_hud_rebind(void) {
  const qa_catalog_product_expectation *q2 =
      expectation_by_id("q2-classic-baseq2");
  CHECK(q2 != NULL);
  qa_provider_reference source = {"q2:official", "q2-classic-baseq2"};
  qa_weapon_hud_icons icons = {0};
  bool found = false;
  CHECK(qa_weapon_hud_icons_for(source, q2, "q2:weapon_rocketlauncher",
                                &icons, &found));
  CHECK(found && icons.has_weapon);
  CHECK(icons.weapon.kind == QA_WEAPON_HUD_ICON_IMAGE);
  CHECK(strcmp(icons.weapon.resource.path, "pics/w_rlauncher.pcx") == 0);
  CHECK(icons.has_ammo);
  CHECK(strcmp(icons.ammo.resource.path, "pics/a_rockets.pcx") == 0);
  qa_weapon_hud_icons_free(&icons);

  const qa_catalog_product_expectation *q3 =
      expectation_by_id("q3-baseq3");
  CHECK(q3 != NULL);
  qa_provider_reference q3_source = {"q3:official", "q3-baseq3"};
  memset(&icons, 0, sizeof icons);
  found = false;
  CHECK(qa_weapon_hud_icons_for(q3_source, q3, "q3:weapon/railgun",
                                &icons, &found));
  CHECK(found && icons.has_weapon);
  CHECK(icons.weapon.kind == QA_WEAPON_HUD_ICON_SHADER);
  CHECK(strcmp(icons.weapon.name, "icons/iconw_railgun") == 0);
  CHECK(icons.has_ammo);
  CHECK(strcmp(icons.ammo.name, "icons/icona_railgun") == 0);
  qa_weapon_hud_icons_free(&icons);
  /* The baseq3 itemList has no nailgun row: TS returns null. */
  memset(&icons, 0, sizeof icons);
  found = true;
  CHECK(qa_weapon_hud_icons_for(q3_source, q3, "q3:weapon/nailgun",
                                &icons, &found));
  CHECK(!found);
  qa_weapon_hud_icons_free(&icons);
}

static void test_launch_and_source_program(void) {
  qa_launch_preset preset = {0};
  preset.id = "recipe:preset:1";
  qa_launch_choice choice = qa_preset_choice("recipe:preset:1");
  qa_selected_launch selected = qa_select_launch(choice, &preset, NULL);
  CHECK(strcmp(selected.base.id, "recipe:preset:1") == 0);
  CHECK(strcmp(selected.preset, "recipe:preset:1") == 0);
  qa_launch_choice wrong = qa_preset_choice("recipe:other:1");
  CHECK_ABORTS(qa_select_launch(wrong, &preset, NULL));
  char *implementation =
      qa_source_program_implementation(&qa_expected_products[0]);
  CHECK(implementation != NULL &&
        strcmp(implementation, "q1:source/classic/id1") == 0);
  free(implementation);
  qa_provider_reference entities = {"q1:official", "q1:classic:hipnotic:installed"};
  bool found = false;
  char *campaign = qa_selected_source_program(NULL, 0, entities, &found);
  CHECK(found && campaign != NULL && strcmp(campaign, "hipnotic") == 0);
  free(campaign);
}

static void test_monsters_and_behaviors(void) {
  qa_enemy_selection enemies = {0};
  enemies.kind = QA_ENEMY_SELECTION_MAP_DEFINED;
  size_t count = 99;
  qa_monster_definition_reference *definitions =
      qa_selected_monster_definitions(enemies, &count);
  CHECK(count == 0 && definitions == NULL);
  /* Empty authored classname aborts (catalog unused on this path
   * because all targets are map-defined). */
  qa_monster_by_classname entry = {"", {true, {{NULL, NULL}, NULL}}};
  qa_enemy_selection replace = {0};
  replace.kind = QA_ENEMY_SELECTION_REPLACE;
  replace.default_target.is_map_defined = true;
  replace.by_classname = &entry;
  replace.by_classname_count = 1;
  qa_content_catalog empty = {0};
  CHECK_ABORTS(qa_validate_monsters(replace, &empty));
  /* Behavior catalog duplicate detection. */
  qa_weapon_behavior_catalog catalog;
  qa_weapon_behavior_catalog_init(&catalog);
  qa_weapon_behavior_definition definition = {0};
  definition.id = "test:rocket";
  definition.role = "rocket";
  CHECK(qa_weapon_behavior_catalog_add(&catalog, &definition));
  CHECK(qa_weapon_behavior_catalog_require(&catalog, "test:rocket") == &definition);
  CHECK_ABORTS(qa_weapon_behavior_catalog_add(&catalog, &definition));
  CHECK_ABORTS(qa_weapon_behavior_catalog_require(&catalog, "test:missing"));
  size_t role_count = 0;
  const qa_weapon_behavior_definition **for_role =
      qa_weapon_behavior_catalog_for_role(&catalog, "rocket", &role_count);
  CHECK(role_count == 1);
  free((void *)for_role);
  qa_weapon_behavior_catalog_destroy(&catalog);
  /* QC trajectory binding inspection on a crafted program. */
  qa_catalog_qc_statement statements[2] = {
      {30, 0, 1, 2},   /* Address: b=field-offset global, c=2 */
      {42, 3, 2, 0}};  /* StorePFn: a=function global, b=2 */
  qa_catalog_qc_function functions[3] = {
      {0, "null", 0, 0}, {1, "fire", 0, 0}, {2, "thinker", 2, 0}};
  qa_catalog_qc_definition fields[1] = {{"think", "function", 7}};
  qa_catalog_qc_definition globals[2] = {{"unused", "float", 1},
                                         {"thinker", "function", 3}};
  uint8_t words[32] = {0};
  words[1 * 4] = 7; /* global 1 holds think field offset */
  words[3 * 4] = 2; /* global 3 holds functions[2] */
  qa_catalog_qc_program program = {0};
  program.statements = statements;
  program.statement_count = 2;
  program.functions = functions;
  program.function_count = 3;
  program.fields = fields;
  program.field_count = 1;
  program.globals = globals;
  program.global_count = 2;
  program.initial_globals = words;
  program.initial_globals_length = sizeof words;
  program.digest = "sha256:0000000000000000000000000000000000000000000000000000000000000000";
  size_t binding_count = 0;
  qa_qc_trajectory_binding_inspection *bindings =
      qa_inspect_qc_trajectory_bindings(&program, &binding_count);
  CHECK(binding_count == 1);
  CHECK(bindings != NULL && bindings[0].producer_function == 1 &&
        bindings[0].think_function == 2 && bindings[0].statement == 1);
  free(bindings);
}

int main(void) {
  test_products();
  test_remote();
  test_order_archives();
  test_required_programs();
  test_start_maps();
  test_addons();
  test_nightdive();
  test_documents();
  test_timing_equipment();
  test_discovery_and_weapons();
  test_weapon_hud_rebind();
  test_launch_and_source_program();
  test_monsters_and_behaviors();
  printf("content catalog: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
